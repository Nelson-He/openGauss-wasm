/* contrib/wasm/wasm_executor--1.0.sql */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION wasm_executor" to load this file. \quit

DROP SCHEMA IF EXISTS wasm CASCADE;
CREATE SCHEMA wasm;

CREATE TABLE wasm.instances(
    id           bigint,
    wasm_file    text
);

CREATE TABLE wasm.exported_functions(
    instanceid    bigint,
    namespace     text,
    funcname      text,
    inputs        text,
    outputs       text
);

CREATE FUNCTION wasm_get_instances(
    OUT id bigint,
    OUT wasm_file text
)
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'wasm_get_instances'
LANGUAGE C STRICT;

CREATE FUNCTION wasm_get_exported_functions(
    IN id bigint,
    OUT funcname text,
    OUT inputs   text,
    OUT outputs  text
)
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'wasm_get_exported_functions'
LANGUAGE C STRICT;

CREATE FUNCTION wasm_create_new_instance(text)
RETURNS int8
AS 'MODULE_PATHNAME', 'wasm_create_instance'
LANGUAGE C STRICT;

CREATE FUNCTION wasm_invoke_function_0(text, text)
RETURNS int8
AS 'MODULE_PATHNAME', 'wasm_invoke_function_0'
LANGUAGE C STRICT;

CREATE FUNCTION wasm_invoke_function_1(text, text, int8)
RETURNS int8
AS 'MODULE_PATHNAME', 'wasm_invoke_function_1'
LANGUAGE C STRICT;

CREATE FUNCTION wasm_invoke_function_2(text, text, int8, int8)
RETURNS int8
AS 'MODULE_PATHNAME', 'wasm_invoke_function_2'
LANGUAGE C STRICT;

CREATE FUNCTION wasm_invoke_function_3(text, text, int8, int8, int8)
RETURNS int8
AS 'MODULE_PATHNAME', 'wasm_invoke_function_3'
LANGUAGE C STRICT;

CREATE FUNCTION wasm_invoke_function_4(text, text, int8, int8, int8, int8)
RETURNS int8
AS 'MODULE_PATHNAME', 'wasm_invoke_function_4'
LANGUAGE C STRICT;

CREATE FUNCTION wasm_invoke_function_5(text, text, int8, int8, int8, int8, int8)
RETURNS int8
AS 'MODULE_PATHNAME', 'wasm_invoke_function_5'
LANGUAGE C STRICT;

CREATE FUNCTION wasm_invoke_function_6(text, text, int8, int8, int8, int8, int8, int8)
RETURNS int8
AS 'MODULE_PATHNAME', 'wasm_invoke_function_6'
LANGUAGE C STRICT;

CREATE FUNCTION wasm_invoke_function_7(text, text, int8, int8, int8, int8, int8, int8, int8)
RETURNS int8
AS 'MODULE_PATHNAME', 'wasm_invoke_function_7'
LANGUAGE C STRICT;

CREATE FUNCTION wasm_invoke_function_8(text, text, int8, int8, int8, int8, int8, int8, int8, int8)
RETURNS int8
AS 'MODULE_PATHNAME', 'wasm_invoke_function_8'
LANGUAGE C STRICT;

CREATE FUNCTION wasm_invoke_function_9(text, text, int8, int8, int8, int8, int8, int8, int8, int8, int8)
RETURNS int8
AS 'MODULE_PATHNAME', 'wasm_invoke_function_9'
LANGUAGE C STRICT;

CREATE FUNCTION wasm_invoke_function_10(text, text, int8, int8, int8, int8, int8, int8, int8, int8, int8, int8)
RETURNS int8
AS 'MODULE_PATHNAME', 'wasm_invoke_function_10'
LANGUAGE C STRICT;

CREATE OR REPLACE FUNCTION wasm_new_instance(module_pathname text, namespace text) RETURNS text AS $$
DECLARE
    current_instance_id int8;
    exported_function RECORD;
    exported_function_generated_inputs text;
    exported_function_generated_outputs text;
BEGIN
    -- Create a new instance, and stores its ID in `current_instance_id`.
    SELECT wasm_create_new_instance(module_pathname) INTO STRICT current_instance_id;
   
    -- Insert the wasm information to gloable table 
    INSERT INTO wasm.instances SELECT id, wasm_file FROM wasm_get_instances() WHERE id = current_instance_id;
    INSERT INTO wasm.exported_functions SELECT current_instance_id, namespace, funcname, inputs, outputs FROM wasm_get_exported_functions(current_instance_id);
    
    -- Generate functions for each exported functions from the WebAssembly instance.
    FOR
        exported_function
    IN
        SELECT
            funcname,
            inputs,
            CASE
                WHEN length(inputs) = 0 THEN 0
                ELSE array_length(regexp_split_to_array(inputs, ','), 1)
            END AS input_arity,
            outputs
        FROM
            (SELECT * FROM wasm_get_exported_functions(current_instance_id))
    LOOP
        IF exported_function.input_arity > 10 THEN
           RAISE EXCEPTION 'WebAssembly exported function `%` has an arity greater than 10, which is not supported yet.', exported_function.funcname;
        END IF;

        exported_function_generated_inputs := '';
        exported_function_generated_outputs := '';

        FOR nth IN 1..exported_function.input_arity LOOP
            exported_function_generated_inputs := exported_function_generated_inputs || format(', CAST($%s AS int8)', nth);
        END LOOP;

        IF length(exported_function.outputs) > 0 THEN
            exported_function_generated_outputs := exported_function.outputs;
        ELSE
            exported_function_generated_outputs := 'integer';
        END IF;

        EXECUTE format(
            'CREATE OR REPLACE FUNCTION %I_%I(%3$s) RETURNS %5$s AS $F$' ||
            'DECLARE' ||
            '    output %5$s;' ||
            'BEGIN' ||
            '    SELECT wasm_invoke_function_%4$s(%6$L, %2$L%7$s) INTO STRICT output;' ||
            '    RETURN output;' ||
            'END;' ||
            '$F$ LANGUAGE plpgsql;',
            namespace, -- 1
            exported_function.funcname, -- 2
            exported_function.inputs, -- 3
            exported_function.input_arity, -- 4
            exported_function_generated_outputs, -- 5
            current_instance_id, -- 6
            exported_function_generated_inputs -- 7
        );
    END LOOP;

    RETURN current_instance_id;
END;
$$ LANGUAGE plpgsql;