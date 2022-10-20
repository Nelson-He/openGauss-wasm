#include "postgres.h"
#include "knl/knl_variable.h"
#include "utils/builtins.h"
#include "utils/uuid.h"
#include "miscadmin.h"
#include "funcapi.h"
#include <string>
#include <vector>
#include <map>

#include "wasm.h"
#include "wasmtime.h"

PG_MODULE_MAGIC;


extern "C" Datum wasm_create_instance_wat(PG_FUNCTION_ARGS);
extern "C" Datum wasm_create_instance(PG_FUNCTION_ARGS);
extern "C" Datum wasm_get_instances(PG_FUNCTION_ARGS);
extern "C" Datum wasm_get_exported_functions(PG_FUNCTION_ARGS);
extern "C" Datum wasm_invoke_function_0(PG_FUNCTION_ARGS);
extern "C" Datum wasm_invoke_function_1(PG_FUNCTION_ARGS);
extern "C" Datum wasm_invoke_function_2(PG_FUNCTION_ARGS);
extern "C" Datum wasm_invoke_function_3(PG_FUNCTION_ARGS);
extern "C" Datum wasm_invoke_function_4(PG_FUNCTION_ARGS);
extern "C" Datum wasm_invoke_function_5(PG_FUNCTION_ARGS);
extern "C" Datum wasm_invoke_function_6(PG_FUNCTION_ARGS);
extern "C" Datum wasm_invoke_function_7(PG_FUNCTION_ARGS);
extern "C" Datum wasm_invoke_function_8(PG_FUNCTION_ARGS);
extern "C" Datum wasm_invoke_function_9(PG_FUNCTION_ARGS);
extern "C" Datum wasm_invoke_function_10(PG_FUNCTION_ARGS);

typedef struct WasmInstInfo {
    wasm_engine_t *wasm_engine;

    wasmtime_store_t *wasm_store;

    wasmtime_module_t *wasm_module;

    wasmtime_instance_t instance;

    std::string wasm_file;
} WasmInstInfo;

typedef struct TupleInstanceState {
    TupleDesc tupd;
    std::map<int64, WasmInstInfo*>::iterator currindex;
    std::map<int64, WasmInstInfo*>::iterator lastindex;
} TupleInstanceState;

typedef struct WasmFuncInfo {
    std::string funcname;
    std::string inputs;
    std::string outputs;
} WasmFuncInfo;

typedef struct TupleFuncState {
    TupleDesc tupd;
    std::vector<WasmFuncInfo*>::iterator currindex;
    std::vector<WasmFuncInfo*>::iterator lastindex;
} TupleFuncState;

// Store the wasm instance info globally 
static std::map<int64, WasmInstInfo*> instances; 

// Store the wasm exported function info globally
static std::map<int64, std::vector<WasmFuncInfo *>*> exported_functions;

static WasmInstInfo* find_instance(int64 instanceid)
{
    std::map<int64, WasmInstInfo*>::iterator itor = instances.begin();
    while (itor != instances.end()) {
        if (itor->first == instanceid) {
            return itor->second;
        }
        itor++;
    }
    elog(DEBUG1, "wasm_executor: not find instance info for instanceid %ld", instanceid);
    return NULL;
}

static std::vector<WasmFuncInfo*>* find_exported_func_list(int64 instanceid)
{
    std::map<int64, std::vector<WasmFuncInfo*>*>::iterator itor = exported_functions.begin();
    while (itor != exported_functions.end()) {
        if (itor->first == instanceid) {
            return itor->second;
        }
        itor++;
    }
    elog(DEBUG1, "wasm_executor: not find exported func info for instanceid %ld", instanceid);
    return NULL;
}

static int64 generate_uuid(Datum input) 
{
    Datum uuid = DirectFunctionCall1(uuid_hash, input);
    return DatumGetInt64(uuid);
}

static void exit_with_error(const char *message, wasmtime_error_t *error, wasm_trap_t *trap)
{
    wasm_byte_vec_t error_message;
    if (error != NULL) {
        wasmtime_error_message(error, &error_message);
    } else {
        wasm_trap_message(trap, &error_message);
    }
    char *messaga_info = pstrdup(error_message.data);
    wasm_byte_vec_delete(&error_message);

    ereport(ERROR, (errmsg("wasm_executor: %s:%s", message, messaga_info)));
}

static int64 wasm_invoke_function(char *instanceid_str, char* funcname, std::vector<int64> &args)
{
    int64 instanceid = atol(instanceid_str);
    WasmInstInfo* instanceinfo = find_instance(instanceid);
    if (instanceinfo == NULL) {
        ereport(ERROR, (errmsg("wasm_executor: instance with id %ld is not find", instanceid)));
    }
    
    wasmtime_extern_t wasm_extern;
    wasmtime_context_t* context = wasmtime_store_context(instanceinfo->wasm_store);
    int funcnamelen = strlen(funcname);
    bool ok = wasmtime_instance_export_get(context, &instanceinfo->instance, funcname, funcnamelen, &wasm_extern);
    if (!ok || wasm_extern.kind != WASMTIME_EXTERN_FUNC) {
        ereport(ERROR, (errmsg("wasm_executor: not find the exported function with name(%s) and namelen(%d)", 
            funcname, funcnamelen)));
    }

    wasmtime_func_t wasm_func = wasm_extern.of.func;
    wasm_functype_t* wasm_functype = wasmtime_func_type(context, &wasm_func);
    
    const wasm_valtype_vec_t* wasm_params = wasm_functype_params(wasm_functype);
    if (wasm_params->size != args.size()) {
        ereport(ERROR, (errmsg("wasm_executor: function parameters not matched")));
    }

    wasmtime_val_t call_params[args.size()];
    for (unsigned int i = 0; i < wasm_params->size; ++i) {
        if (wasm_valtype_kind(wasm_params->data[i]) == WASM_I32) {
            call_params[i].kind = WASMTIME_I32;
            call_params[i].of.i32 = args[i];
        } else if (wasm_valtype_kind(wasm_params->data[i]) == WASM_I64) {
            call_params[i].kind = WASMTIME_I64;
            call_params[i].of.i64 = args[i];
        } else {
            ereport(ERROR, (errmsg("wasm_executor: not support the value type(%d) for now", wasm_valtype_kind(wasm_params->data[i]))));
        }
    }

    wasmtime_val_t results[1];
    wasm_trap_t *wasm_trap = NULL;
    wasmtime_error_t *error_msg = wasmtime_func_call(context, &wasm_func, call_params, args.size(), results, 1, &wasm_trap);
    if (error_msg != NULL || wasm_trap != NULL) {
        exit_with_error("failed to call function", error_msg, wasm_trap);
    }

    int64 ret_val = 0;
    if (results[0].kind == WASMTIME_I32) {
        ret_val = results[0].of.i32;
    } else if (results[0].kind == WASMTIME_I64) {
        ret_val = results[0].of.i64;
    } else {
        ereport(ERROR, (errmsg("wasm_executor: the function(%s) return type(%d) not supported", funcname, results[0].kind)));
    }

    return ret_val;
}

static void wasm_export_funcs_query(int64 instanceid, TupleFuncState* inter_call_data)
{
    WasmInstInfo* instanceinfo = find_instance(instanceid);
    if (instanceinfo == NULL) {
        ereport(ERROR, (errmsg("wasm_executor: instance with id %ld is not find", instanceid)));
    }

    std::vector<WasmFuncInfo *>* functions = find_exported_func_list(instanceid);
    if (functions != NULL) {
        inter_call_data->currindex = functions->begin();
        inter_call_data->lastindex = functions->end();
        elog(DEBUG1, "wasm_executor:find exported func info for instanceid %ld", instanceid);
        return;
    }

    functions = new(std::nothrow)std::vector<WasmFuncInfo *>;
    exported_functions.insert(std::pair<int64, std::vector<WasmFuncInfo *>*>(instanceid, functions));

    wasmtime_context_t* context = wasmtime_store_context(instanceinfo->wasm_store);
    wasmtime_instance_t& instance = instanceinfo->instance;

    char *export_name = NULL;
    size_t namelen;
    wasmtime_extern_t wasm_extern;
    int index = 0;
    while (wasmtime_instance_export_nth(context, &instance, index, &export_name, &namelen, &wasm_extern)) {
        if (wasm_extern.kind == WASMTIME_EXTERN_FUNC) {
            wasmtime_func_t wasm_func = wasm_extern.of.func;
            wasm_functype_t* wasm_functype = wasmtime_func_type(context, &wasm_func);
            WasmFuncInfo *funcinfo = new(std::nothrow)WasmFuncInfo();

            const wasm_valtype_vec_t* wasm_results = wasm_functype_results(wasm_functype);
            if (wasm_results->size != 1) {
                ereport(ERROR, (errmsg("wasm_executor: only support the functions who will return exactly one result value for now")));
            }
            //TODO:support more data types
            if (wasm_valtype_kind(wasm_results->data[0]) == WASM_I32) {
                funcinfo->outputs = "integer";
            } else if (wasm_valtype_kind(wasm_results->data[0]) == WASM_I64) {
                funcinfo->outputs = "bigint";
            } else {
                ereport(ERROR, (errmsg("wasm_executor: not support the value type(%d) for now", wasm_valtype_kind(wasm_results->data[0]))));
            }

            const wasm_valtype_vec_t* wasm_params = wasm_functype_params(wasm_functype);
            if (wasm_params->size > 10) {
                ereport(ERROR, (errmsg("wasm_executor: do not support function with 10 more parameters")));
            }

            for (unsigned int i = 0; i < wasm_params->size; ++i) {
                if (wasm_valtype_kind(wasm_params->data[i]) == WASM_I32) {
                    funcinfo->inputs += "integer,";
                } else if (wasm_valtype_kind(wasm_params->data[i]) == WASM_I64) {
                    funcinfo->inputs += "bigint,";
                } else {
                    ereport(ERROR, (errmsg("wasm_executor: not support the value type(%d) for now", wasm_valtype_kind(wasm_params->data[i]))));
                }
            }
            if (funcinfo->inputs.length() > 0) {
                funcinfo->inputs.pop_back();
            }

            funcinfo->funcname = std::string(export_name, namelen);
            functions->push_back(funcinfo);
        }
        index++;
    }
    
    inter_call_data->currindex = functions->begin();
    inter_call_data->lastindex = functions->end();
    elog(DEBUG1, "wasm_executor:init exported func info for instanceid %ld", instanceid);
}

PG_FUNCTION_INFO_V1(wasm_create_instance_wat);
Datum wasm_create_instance_wat(PG_FUNCTION_ARGS) 
{
    int64 uuid = generate_uuid(PG_GETARG_DATUM(0));
    text *arg = PG_GETARG_TEXT_P(0);
    char* filepath = text_to_cstring(arg);
    canonicalize_path(filepath);
    
    if (!superuser())
        ereport(ERROR,
            (errcode(ERRCODE_INSUFFICIENT_PRIVILEGE), (errmsg("wasm_executor: must be system admin to create wasm instance"))));

    WasmInstInfo *instinfo = find_instance(uuid);
    if (instinfo != NULL) {
        ereport(NOTICE, (errmsg("wasm_executor: instance already created for %s", filepath)));
        return UInt32GetDatum(uuid);
    }

    instinfo = new (std::nothrow)WasmInstInfo();
    
    instinfo->wasm_engine = wasm_engine_new();
    if (instinfo->wasm_engine == NULL) {
        ereport(ERROR, (errmsg("wasm_executor: unable to create new wasm engine")));
    }
    instinfo->wasm_store = wasmtime_store_new(instinfo->wasm_engine, NULL, NULL);
    if (instinfo->wasm_store == NULL) {
        ereport(ERROR, (errmsg("wasm_executor: unable to create new wasmtime storage")));
    }
    wasmtime_context_t *context = wasmtime_store_context(instinfo->wasm_store);
    
    wasm_byte_vec_t wat_bytes;
    FILE *file = fopen(filepath, "r");
    if (file == NULL) {
        ereport(ERROR, (errmsg("wasm_executor: unable to open file %s", filepath)));
    }

    fseek(file, 0L, SEEK_END);
    size_t file_size = ftell(file);
    wasm_byte_vec_new_uninitialized(&wat_bytes, file_size);
    fseek(file, 0L, SEEK_SET);
    int ret = fread(wat_bytes.data, file_size, 1, file);
    fclose(file);
    if (ret != 1) {
        ereport(ERROR, (errmsg("wasm_executor: failed to load moude from %s", filepath)));
    }

    wasm_byte_vec_t wasm_bytes;
    wasmtime_error_t *error_msg = wasmtime_wat2wasm(wat_bytes.data, wat_bytes.size, &wasm_bytes);
    if (error_msg != NULL) {
        exit_with_error("failed to parse wat", error_msg, NULL);
    }      
    wasm_byte_vec_delete(&wat_bytes);

    error_msg = wasmtime_module_new(instinfo->wasm_engine, (uint8 *)wasm_bytes.data, wasm_bytes.size, &instinfo->wasm_module);
    if (instinfo->wasm_module == NULL) {
        exit_with_error("failed to compile module", error_msg, NULL);
    }
    wasm_byte_vec_delete(&wasm_bytes);

    wasm_trap_t *wasm_trap = NULL;
    error_msg = wasmtime_instance_new(context, instinfo->wasm_module, NULL, 0, &instinfo->instance, &wasm_trap);
    if (error_msg != NULL || wasm_trap != NULL) {
        exit_with_error("failed to create wasm instance", error_msg, wasm_trap);
    }

    instinfo->wasm_file = filepath;
    instances.insert(std::pair<int64, WasmInstInfo*>(uuid, instinfo));

    return Int64GetDatum(uuid);
}

PG_FUNCTION_INFO_V1(wasm_create_instance);
Datum wasm_create_instance(PG_FUNCTION_ARGS) 
{
    int64 uuid = generate_uuid(PG_GETARG_DATUM(0));
    text *arg = PG_GETARG_TEXT_P(0);
    char* filepath = text_to_cstring(arg);
    canonicalize_path(filepath);
    
    if (!superuser())
        ereport(ERROR,
            (errcode(ERRCODE_INSUFFICIENT_PRIVILEGE), (errmsg("wasm_executor: must be system admin to create wasm instance"))));

    WasmInstInfo *instinfo = find_instance(uuid);
    if (instinfo != NULL) {
        ereport(NOTICE, (errmsg("wasm_executor: instance already created for %s", filepath)));
        return UInt32GetDatum(uuid);
    }

    instinfo = new (std::nothrow)WasmInstInfo();
    
    instinfo->wasm_engine = wasm_engine_new();
    if (instinfo->wasm_engine == NULL) {
        ereport(ERROR, (errmsg("wasm_executor: unable to create new wasm engine")));
    }
    instinfo->wasm_store = wasmtime_store_new(instinfo->wasm_engine, NULL, NULL);
    if (instinfo->wasm_store == NULL) {
        ereport(ERROR, (errmsg("wasm_executor: unable to create new wasmtime storage")));
    }
    wasmtime_context_t *context = wasmtime_store_context(instinfo->wasm_store);
    
    wasm_byte_vec_t wasm_bytes;
    FILE *file = fopen(filepath, "rb");
    if (file == NULL) {
        ereport(ERROR, (errmsg("wasm_executor: unable to open file %s", filepath)));
    }

    fseek(file, 0L, SEEK_END);
    size_t file_size = ftell(file);
    wasm_byte_vec_new_uninitialized(&wasm_bytes, file_size);
    fseek(file, 0L, SEEK_SET);
    int ret = fread(wasm_bytes.data, file_size, 1, file);
    fclose(file);
    if (ret != 1) {
        ereport(ERROR, (errmsg("wasm_executor: failed to load moude from %s", filepath)));
    }

    wasmtime_error_t *error_msg = wasmtime_module_new(instinfo->wasm_engine, (uint8 *)wasm_bytes.data, wasm_bytes.size, &instinfo->wasm_module);
    if (instinfo->wasm_module == NULL) {
        exit_with_error("failed to compile module", error_msg, NULL);
    }
    wasm_byte_vec_delete(&wasm_bytes);

    wasm_trap_t *wasm_trap = NULL;
    error_msg = wasmtime_instance_new(context, instinfo->wasm_module, NULL, 0, &instinfo->instance, &wasm_trap);
    if (error_msg != NULL || wasm_trap != NULL) {
        exit_with_error("failed to create wasm instance", error_msg, wasm_trap);
    }

    instinfo->wasm_file = filepath;
    instances.insert(std::pair<int64, WasmInstInfo*>(uuid, instinfo));

    return Int64GetDatum(uuid);
}

PG_FUNCTION_INFO_V1(wasm_get_instances);
Datum wasm_get_instances(PG_FUNCTION_ARGS) 
{
    FuncCallContext* fctx = NULL;
    TupleInstanceState* inter_call_data = NULL;
    if (SRF_IS_FIRSTCALL()) {
        TupleDesc tupdesc;
        MemoryContext mctx;

        fctx = SRF_FIRSTCALL_INIT();
        mctx = MemoryContextSwitchTo(fctx->multi_call_memory_ctx);
        inter_call_data = (TupleInstanceState*)palloc(sizeof(TupleInstanceState));

        /* Build a tuple descriptor for our result type */
        if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
            elog(ERROR, "wasm_executor: return type must be a row type");

        inter_call_data->tupd = tupdesc;
        inter_call_data->currindex = instances.begin();
        inter_call_data->lastindex = instances.end();

        fctx->user_fctx = inter_call_data;
        MemoryContextSwitchTo(mctx);
    }

    fctx = SRF_PERCALL_SETUP();
    inter_call_data = (TupleInstanceState*)(fctx->user_fctx);
    
    if (inter_call_data->currindex != inter_call_data->lastindex) {
        HeapTuple resultTuple;
        Datum result;
        Datum values[2];
        bool nulls[2];

        errno_t rc = memset_s(nulls, sizeof(nulls), 0, sizeof(nulls));
        securec_check_c(rc, "\0", "\0");

        WasmInstInfo *instanceinfo = inter_call_data->currindex->second;
        values[0] = Int64GetDatum(inter_call_data->currindex->first);
        values[1] = CStringGetTextDatum(instanceinfo->wasm_file.c_str());

        /* Build and return the result tuple. */
        resultTuple = heap_form_tuple(inter_call_data->tupd, values, nulls);
        result = HeapTupleGetDatum(resultTuple);

        inter_call_data->currindex++;
        SRF_RETURN_NEXT(fctx, result);
    } else {
        SRF_RETURN_DONE(fctx);
    }
}

PG_FUNCTION_INFO_V1(wasm_get_exported_functions);
Datum wasm_get_exported_functions(PG_FUNCTION_ARGS) 
{
    int64 instanceid = PG_GETARG_INT64(0);
    FuncCallContext* fctx = NULL;
    TupleFuncState* inter_call_data = NULL;
    if (SRF_IS_FIRSTCALL()) {
        TupleDesc tupdesc;
        MemoryContext mctx;

        fctx = SRF_FIRSTCALL_INIT();
        mctx = MemoryContextSwitchTo(fctx->multi_call_memory_ctx);
        inter_call_data = (TupleFuncState*)palloc(sizeof(TupleFuncState));
        
        /* Build a tuple descriptor for our result type */
        if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
            elog(ERROR, "wasm_executor: return type must be a row type");

        inter_call_data->tupd = tupdesc;
        wasm_export_funcs_query(instanceid, inter_call_data);

        fctx->user_fctx = inter_call_data;
        MemoryContextSwitchTo(mctx);
    }
    fctx = SRF_PERCALL_SETUP();
    inter_call_data = (TupleFuncState*)(fctx->user_fctx);
    
    if (inter_call_data->currindex != inter_call_data->lastindex) {
        HeapTuple resultTuple;
        Datum result;
        Datum values[3];
        bool nulls[3];

        errno_t rc = memset_s(nulls, sizeof(nulls), 0, sizeof(nulls));
        securec_check_c(rc, "\0", "\0");

        WasmFuncInfo *funcinfo = *inter_call_data->currindex;
        values[0] = CStringGetTextDatum(funcinfo->funcname.c_str());
        values[1] = CStringGetTextDatum(funcinfo->inputs.c_str());
        values[2] = CStringGetTextDatum(funcinfo->outputs.c_str());

        /* Build and return the result tuple. */
        resultTuple = heap_form_tuple(inter_call_data->tupd, values, nulls);
        result = HeapTupleGetDatum(resultTuple);
        inter_call_data->currindex++;

        SRF_RETURN_NEXT(fctx, result);
    } else {
        SRF_RETURN_DONE(fctx);
    }
}

PG_FUNCTION_INFO_V1(wasm_invoke_function_0);
Datum wasm_invoke_function_0(PG_FUNCTION_ARGS)
{
    char* instanceid = TextDatumGetCString(PG_GETARG_DATUM(0));
    char* funcname = TextDatumGetCString(PG_GETARG_DATUM(1));
    std::vector<int64> params;

    int64 result = wasm_invoke_function(instanceid, funcname, params);
    return Int64GetDatum(result);
}

PG_FUNCTION_INFO_V1(wasm_invoke_function_1);
Datum wasm_invoke_function_1(PG_FUNCTION_ARGS)
{
    char* instanceid = TextDatumGetCString(PG_GETARG_DATUM(0));
    char* funcname = TextDatumGetCString(PG_GETARG_DATUM(1));
    std::vector<int64> params;

    params.push_back(PG_GETARG_INT64(2));
    int64 result = wasm_invoke_function(instanceid, funcname, params);
    return Int64GetDatum(result);
}

PG_FUNCTION_INFO_V1(wasm_invoke_function_2);
Datum wasm_invoke_function_2(PG_FUNCTION_ARGS)
{
    char* instanceid = TextDatumGetCString(PG_GETARG_DATUM(0));
    char* funcname = TextDatumGetCString(PG_GETARG_DATUM(1));
    std::vector<int64> params;

    params.push_back(PG_GETARG_INT64(2));
    params.push_back(PG_GETARG_INT64(3));
    int64 result = wasm_invoke_function(instanceid, funcname, params);
    return Int64GetDatum(result);
}

PG_FUNCTION_INFO_V1(wasm_invoke_function_3);
Datum wasm_invoke_function_3(PG_FUNCTION_ARGS)
{
    char* instanceid = TextDatumGetCString(PG_GETARG_DATUM(0));
    char* funcname = TextDatumGetCString(PG_GETARG_DATUM(1));
    std::vector<int64> params;

    params.push_back(PG_GETARG_INT64(2));
    params.push_back(PG_GETARG_INT64(3));
    params.push_back(PG_GETARG_INT64(4));
    int64 result = wasm_invoke_function(instanceid, funcname, params);
    return Int64GetDatum(result);
}

PG_FUNCTION_INFO_V1(wasm_invoke_function_4);
Datum wasm_invoke_function_4(PG_FUNCTION_ARGS)
{
    char* instanceid = TextDatumGetCString(PG_GETARG_DATUM(0));
    char* funcname = TextDatumGetCString(PG_GETARG_DATUM(1));
    std::vector<int64> params;

    params.push_back(PG_GETARG_INT64(2));
    params.push_back(PG_GETARG_INT64(3));
    params.push_back(PG_GETARG_INT64(4));
    params.push_back(PG_GETARG_INT64(5));
    int64 result = wasm_invoke_function(instanceid, funcname, params);
    return Int64GetDatum(result);
}

PG_FUNCTION_INFO_V1(wasm_invoke_function_5);
Datum wasm_invoke_function_5(PG_FUNCTION_ARGS)
{
    char* instanceid = TextDatumGetCString(PG_GETARG_DATUM(0));
    char* funcname = TextDatumGetCString(PG_GETARG_DATUM(1));
    std::vector<int64> params;

    params.push_back(PG_GETARG_INT64(2));
    params.push_back(PG_GETARG_INT64(3));
    params.push_back(PG_GETARG_INT64(4));
    params.push_back(PG_GETARG_INT64(5));
    params.push_back(PG_GETARG_INT64(6));
    int64 result = wasm_invoke_function(instanceid, funcname, params);
    return Int64GetDatum(result);
}

PG_FUNCTION_INFO_V1(wasm_invoke_function_6);
Datum wasm_invoke_function_6(PG_FUNCTION_ARGS)
{
    char* instanceid = TextDatumGetCString(PG_GETARG_DATUM(0));
    char* funcname = TextDatumGetCString(PG_GETARG_DATUM(1));
    std::vector<int64> params;

    params.push_back(PG_GETARG_INT64(2));
    params.push_back(PG_GETARG_INT64(3));
    params.push_back(PG_GETARG_INT64(4));
    params.push_back(PG_GETARG_INT64(5));
    params.push_back(PG_GETARG_INT64(6));
    params.push_back(PG_GETARG_INT64(7));
    int64 result = wasm_invoke_function(instanceid, funcname, params);
    return Int64GetDatum(result);
}

PG_FUNCTION_INFO_V1(wasm_invoke_function_7);
Datum wasm_invoke_function_7(PG_FUNCTION_ARGS)
{
    char* instanceid = TextDatumGetCString(PG_GETARG_DATUM(0));
    char* funcname = TextDatumGetCString(PG_GETARG_DATUM(1));
    std::vector<int64> params;

    params.push_back(PG_GETARG_INT64(2));
    params.push_back(PG_GETARG_INT64(3));
    params.push_back(PG_GETARG_INT64(4));
    params.push_back(PG_GETARG_INT64(5));
    params.push_back(PG_GETARG_INT64(6));
    params.push_back(PG_GETARG_INT64(7));
    params.push_back(PG_GETARG_INT64(8));
    int64 result = wasm_invoke_function(instanceid, funcname, params);
    return Int64GetDatum(result);
}

PG_FUNCTION_INFO_V1(wasm_invoke_function_8);
Datum wasm_invoke_function_8(PG_FUNCTION_ARGS)
{
    char* instanceid = TextDatumGetCString(PG_GETARG_DATUM(0));
    char* funcname = TextDatumGetCString(PG_GETARG_DATUM(1));
    std::vector<int64> params;

    params.push_back(PG_GETARG_INT64(2));
    params.push_back(PG_GETARG_INT64(3));
    params.push_back(PG_GETARG_INT64(4));
    params.push_back(PG_GETARG_INT64(5));
    params.push_back(PG_GETARG_INT64(6));
    params.push_back(PG_GETARG_INT64(7));
    params.push_back(PG_GETARG_INT64(8));
    params.push_back(PG_GETARG_INT64(9));
    int64 result = wasm_invoke_function(instanceid, funcname, params);
    return Int64GetDatum(result);
}

PG_FUNCTION_INFO_V1(wasm_invoke_function_9);
Datum wasm_invoke_function_9(PG_FUNCTION_ARGS)
{
    char* instanceid = TextDatumGetCString(PG_GETARG_DATUM(0));
    char* funcname = TextDatumGetCString(PG_GETARG_DATUM(1));
    std::vector<int64> params;
    
    params.push_back(PG_GETARG_INT64(2));
    params.push_back(PG_GETARG_INT64(3));
    params.push_back(PG_GETARG_INT64(4));
    params.push_back(PG_GETARG_INT64(5));
    params.push_back(PG_GETARG_INT64(6));
    params.push_back(PG_GETARG_INT64(7));
    params.push_back(PG_GETARG_INT64(8));
    params.push_back(PG_GETARG_INT64(9));
    params.push_back(PG_GETARG_INT64(10));
    int64 result = wasm_invoke_function(instanceid, funcname, params);
    return Int64GetDatum(result);
}

PG_FUNCTION_INFO_V1(wasm_invoke_function_10);
Datum wasm_invoke_function_10(PG_FUNCTION_ARGS)
{
    char* instanceid = TextDatumGetCString(PG_GETARG_DATUM(0));
    char* funcname = TextDatumGetCString(PG_GETARG_DATUM(1));
    std::vector<int64> params;

    params.push_back(PG_GETARG_INT64(2));
    params.push_back(PG_GETARG_INT64(3));
    params.push_back(PG_GETARG_INT64(4));
    params.push_back(PG_GETARG_INT64(5));
    params.push_back(PG_GETARG_INT64(6));
    params.push_back(PG_GETARG_INT64(7));
    params.push_back(PG_GETARG_INT64(8));
    params.push_back(PG_GETARG_INT64(9));
    params.push_back(PG_GETARG_INT64(10));
    params.push_back(PG_GETARG_INT64(11));
    int64 result = wasm_invoke_function(instanceid, funcname, params);
    return Int64GetDatum(result);
}
