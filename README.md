A complete and mature WebAssembly runtime for openGauss based on [Wasmtime](https://wasmtime.dev/).
It's an original way to extend your favorite database capabilities.

> Note This project is inspired by [wasmer-postgres](https://github.com/wasmerio/wasmer-postgres)

Features:

  * **Easy to use**: The `wasmtime` API mimics the standard WebAssembly API,
  * **Fast**: `wasmtime` executes the WebAssembly modules as fast as
    possible, close to **native speed**,
  * **Safe**: All calls to WebAssembly will be fast, but more
    importantly, completely safe and sandboxed.

> Note: The project is still in heavy development. This is a
0.1.0 version. Some API are missing and are under implementation. But
it's fun to play with it.

# Installation

The project comes in two parts:

  1. A shared library, and
  2. A PL/pgSQL extension.
  
To compile the former, the wasmtime-c-api header files are required. 
You can download the header file from [here](https://github.com/bytecodealliance/wasmtime/releases).

After that, run `CREATE EXTENSION wasm_executor` in a
openGauss shell. Two new functions will appear: `wasm_new_instance` and `wasm_new_instance_wat`; They must be
called with the absolute path to the shared library. It looks like
this:

```shell
$ # Build the shared library.
$ make

$ # Install the extension in the Postgres opengauss
$ make install

$ # Activate and initialize the extension.
$ gsql -d postgres -c 'CREATE EXTENSION wasm_executor'
```

And you are ready to go!


# Usage & documentation

Consider the `examples/sum.rs` program:

```rust
#[no_mangle]
pub extern fn sum(x: i32, y: i32) -> i32 {
    x + y
}
```

Once compiled to WebAssembly, one obtains a similar WebAssembly binary
to `examples/sum.wasm`. To use the `sum` exported function, first, 
create a new instance of the WebAssembly module, and second, 
call the `sum` function.

To instantiate a WebAssembly module, the `wasm_new_instance` function
must be used. It has two arguments:

  1. The absolute path to the WebAssembly module, and
  2. A namespace used to prefix exported functions in SQL.

For instance, calling
`wasm_new_instance('/path/to/sum.wasm', 'wasm')` will create the
`wasm_sum` function that is a direct call to the `sum` exported function
of the WebAssembly instance. Thus:

```sql
-- New instance of the `sum.wasm` WebAssembly module.
SELECT wasm_new_instance('/absolute/path/to/sum.wasm', 'wasm');

-- Call a WebAssembly exported function!
SELECT wasm_sum(1, 2);

--  wasm_sum
-- --------
--       3
-- (1 row)
```

Isn't it awesome? Calling Rust from openGauss through WebAssembly!

Let's inspect a little bit further the `wasm_sum` function:

```sql
\x
\df+ wasm_sum
Schema              | public
Name                | wasm_sum
Result data type    | integer
Argument data types | integer, integer
Type                | normal
Volatility          | volatile
Parallel            | unsafe
Owner               | ...
Language            | plpgsql
Source code         | ...
Description         |
fencedmode          | f
propackage          | f
prokind             | f
```

The openGauss `wasm_sum` signature is `(integer, integer) -> integer`,
which maps the Rust `sum` signature `(i32, i32) -> i32`.

So far, only the WebAssembly types `i32` and `i64` are
supported; they respectively map to `integer` and `bigint`
in openGauss. Floats are partly implemented for the moment.

# Quickstart

To get your hands on openGauss with wasm, we recommend using the Docker image.
Download the docker image firstlly.

```shell
docker pull heguofeng/opengauss-wasm:1.0.0
```
Then run it.
```shell
docker run -it heguofeng/opengauss-wasm:1.0.0 bash
```
And enjoy it.


## Inspect a WebAssembly instance

The extension provides two ways to initilize a WebAssembly instance. As you can
see from the functions name show above, one way is to use `wasm_new_instance` from
.wasm file compiled from other languages, the other way is to use `wasm_new_instance_wat`
from .wat file, which is the text format of wasm.

And, the extension provides two tables, gathered together in
the `wasm` foreign schema:

  * `wasm.instances` is a table with the `id` and `wasm_file` columns,
    respectively for the instance ID, and the path of the WebAssembly
    module,
  * `wasm.exported_functions` is a table with the `instanceid`,
    `funcname`, `inputs` and `output` columns, respectively for the
    instance ID of the exported function, its name, its input types
    (already formatted for Postgres), and its output types (already
    formatted for Postgres).

Let's see:

```sql
-- Select all WebAssembly instances.
SELECT * FROM wasm.instances;

--      id        |          wasm_file
-- ---------------+-------------------------------
--  3160787445    | /absolute/path/to/sum.wasm
--  1311091567    | /absolute/path/to/gcd.wat
-- (1 row)

-- Select all exported functions for a specific instance.
SELECT
    funcname,
    inputs,
    outputs
FROM
    wasm.exported_functions
WHERE
    instanceid = 3160787445;

--   name  |     inputs      | outputs
-- --------+-----------------+---------
--  wasm_sum | integer,integer | integer
-- (1 row)
```

# Benchmarks

Benchmarks are useless most of the time, but it shows that WebAssembly
can be a credible alternative to procedural languages such as
PL/pgSQL. Please, don't take those numbers for granted, it can change
at any time, but it shows promising results:

<table>
  <thead>
    <tr>
      <th>Benchmark</th>
      <th>Runtime</th>
      <th align="right">Time (ms)</th>
      <th align="right">Ratio</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td rowspan="2">Fibonacci (n = 50)</td>
      <td><code>openGauss-wasm-executor</code></td>
      <td align="right">0.206</td>
      <td align="right">1×</td>
    </tr>
    <tr>
      <td>PL/pgSQL</td>
      <td align="right">0.431</td>
      <td align="right">2×</td>
    </tr>
    <tr>
      <td rowspan="2">Fibonacci (n = 500)</td>
      <td><code>openGauss-wasm-executor</code></td>
      <td align="right">0.217</td>
      <td align="right">1×</td>
    </tr>
    <tr>
      <td>PL/pgSQL</td>
      <td align="right">2.189</td>
      <td align="right">10×</td>
    </tr>
    <tr>
      <td rowspan="2">Fibonacci (n = 5000)</td>
      <td><code>openGauss-wasm-executor</code></td>
      <td align="right">0.257</td>
      <td align="right">1×</td>
    </tr>
    <tr>
      <td>PL/pgSQL</td>
      <td align="right">18.643</td>
      <td align="right">73×</td>
    </tr>
  </tbody>
</table>

# License

The entire project is under the MulanPSL2 License. Please read [the `LICENSE` file][license].

[license]: http://license.coscl.org.cn/MulanPSL2/
