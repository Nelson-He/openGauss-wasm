(module 
 (type (;0;) (func (param i64) (result i64))) 
 (func $fib (type 0) (param i64) (result i64) 
 (local i64) 
 i64.const 0 
 local.set 1 
 block ;; label = @1 
 local.get 0 
 i64.const 2 
 i64.lt_u 
 br_if 0 (;@1;) 
 i64.const 0 
 local.set 1 
 loop ;; label = @2 
 local.get 0 
 i64.const -1 
 i64.add 
 call $fib 
 local.get 1 
 i64.add 
 local.set 1 
 local.get 0 
 i64.const -2 
 i64.add 
 local.tee 0 
 i64.const 1 
 i64.gt_u 
 br_if 0 (;@2;) 
 end 
 end 
 local.get 0 
 local.get 1 
 i64.add) 
 (memory (;0;) 16) 
 (global $__stack_pointer (mut i32) (i32.const 1048576)) 
 (global (;1;) i32 (i32.const 1048576)) 
 (global (;2;) i32 (i32.const 1048576)) 
 (export "memory" (memory 0)) 
 (export "fib" (func $fib)))
