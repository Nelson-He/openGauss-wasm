#include "postgres.h"
#include "knl/knl_variable.h"
#include "utils/builtins.h"
#include "access/hash.h"
#include "miscadmin.h"
#include "funcapi.h"
#include <string>
#include <vector>
#include <map>

#include <wasmedge/wasmedge.h>

PG_MODULE_MAGIC;

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

typedef struct TupleInstanceState {
    TupleDesc tupd;
    std::map<int64, std::string>::iterator currindex;
    std::map<int64, std::string>::iterator lastindex;
} TupleInstanceState;

typedef struct WasmFuncInfo {
    std::string funcname;
    std::vector<std::string> inputs;
    std::string outputs;
} WasmFuncInfo;

typedef struct TupleFuncState {
    TupleDesc tupd;
    std::vector<WasmFuncInfo*>::iterator currindex;
    std::vector<WasmFuncInfo*>::iterator lastindex;
} TupleFuncState;

#define BUF_LEN 256
#define MAX_PARAMS 10
#define MAX_RETURNS 1

// Store the wasm file info globally 
static std::map<int64, std::string> instances; 

// Store the wasm exported function info globally
static std::map<int64, std::vector<WasmFuncInfo *>*> exported_functions;

static std::string find_wasm_file(int64 instanceid)
{
    std::map<int64, std::string>::iterator itor = instances.begin();
    while (itor != instances.end()) {
        if (itor->first == instanceid) {
            return itor->second;
        }
        itor++;
    }
    elog(DEBUG1, "wasm_executor: not find instance info for instanceid %ld", instanceid);
    return "";
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

static WasmFuncInfo* find_exported_func(int64 instanceid, std::string funcname)
{
    std::vector<WasmFuncInfo *>* functions = find_exported_func_list(instanceid);
    if (functions == NULL) {
        ereport(ERROR, (errmsg("wasm_executor: function infos of instance %ld is not find", instanceid)));
    }
    for (std::vector<WasmFuncInfo*>::iterator curr = functions->begin(); curr != functions->end(); curr++) {
        if ((*curr)->funcname == funcname) {
            return *curr;
        }
    }

    ereport(ERROR, (errmsg("wasm_executor: function %s not exist in instance %ld ", funcname.c_str(), instanceid)));
    return NULL;
}

static int64 generate_uuid(Datum input) 
{
    Datum uuid = DirectFunctionCall1(hashtext, input);
    return DatumGetInt64(uuid);
}

static int64 wasm_invoke_function(char *instanceid_str, char* funcname, std::vector<int64> &args)
{
    int64 instanceid = atol(instanceid_str);
    std::string wasm_file = find_wasm_file(instanceid);
    if (wasm_file == "") {
        ereport(ERROR, (errmsg("wasm_executor: instance with id %ld is not find", instanceid)));
    }
    WasmFuncInfo *funcinfo = find_exported_func(instanceid, funcname);

    WasmEdge_ConfigureContext *config_context = WasmEdge_ConfigureCreate();
    WasmEdge_ConfigureAddHostRegistration(config_context, WasmEdge_HostRegistration_Wasi);
    WasmEdge_VMContext *vm_conext = WasmEdge_VMCreate(config_context, NULL);

    WasmEdge_Value params[args.size()];
    for (unsigned int i = 0; i < args.size(); ++i) {
        if (funcinfo->inputs[i] == "integer") {
            params[i] = WasmEdge_ValueGenI32(args[i]);
        } else {
            params[i] = WasmEdge_ValueGenI64(args[i]);
        }
    }

    WasmEdge_Value result[1];
    WasmEdge_String wasm_func = WasmEdge_StringCreateByCString(funcname);
    WasmEdge_Result ret = WasmEdge_VMRunWasmFromFile(vm_conext, wasm_file.c_str(), wasm_func, params, args.size(), result, 1);
    if (!WasmEdge_ResultOK(ret)) {
        WasmEdge_VMDelete(vm_conext);
        WasmEdge_ConfigureDelete(config_context);
        WasmEdge_StringDelete(wasm_func);
        ereport(ERROR, (errmsg("wasm_executor: call func %s failed", funcname)));
    } 
    int64 ret_val = 0;
    if (funcinfo->outputs == "integer") {
        ret_val = WasmEdge_ValueGetI32(result[0]);
    } else {
        ret_val = WasmEdge_ValueGetI64(result[0]);
    }
    /* Resources deallocations. */
    WasmEdge_VMDelete(vm_conext);
    WasmEdge_ConfigureDelete(config_context);
    WasmEdge_StringDelete(wasm_func);

    return ret_val;
}

static void wasm_export_funcs_query(int64 instanceid, TupleFuncState* inter_call_data)
{
    std::string wasm_file = find_wasm_file(instanceid);
    if (wasm_file == "") {
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
    
    WasmEdge_StoreContext *store_cxt = WasmEdge_StoreCreate();
    WasmEdge_VMContext *vm_cxt = WasmEdge_VMCreate(NULL, store_cxt);

    WasmEdge_VMLoadWasmFromFile(vm_cxt, wasm_file.c_str());
    WasmEdge_VMValidate(vm_cxt);
    WasmEdge_VMInstantiate(vm_cxt);
    
    WasmEdge_String func_name_list[BUF_LEN];
    const WasmEdge_FunctionTypeContext *func_type_list[BUF_LEN];
    /*
     * If the list length is larger than the buffer length, the overflowed data
     * will be discarded.
     */
    uint32_t rel_func_num = WasmEdge_VMGetFunctionList(vm_cxt, func_name_list, func_type_list, BUF_LEN);
    for (unsigned int i = 0; i < rel_func_num && i < BUF_LEN; ++i) {
        char tmp_buffer[BUF_LEN];
        uint32_t func_name_len = WasmEdge_StringCopy(func_name_list[i], tmp_buffer, sizeof(tmp_buffer));
        elog(DEBUG1, "wasm_executor: exported function string length: %u, name: %s\n", func_name_len, tmp_buffer);

        uint32_t param_nums = WasmEdge_FunctionTypeGetParametersLength(func_type_list[i]);
        if (param_nums > MAX_PARAMS) {
            WasmEdge_StoreDelete(store_cxt);
            WasmEdge_VMDelete(vm_cxt);
            ereport(ERROR, (errmsg("wasm_executor: func %s has more than 10 params which not support", tmp_buffer)));
        }

        uint32_t return_num = WasmEdge_FunctionTypeGetReturnsLength(func_type_list[i]);
        if (return_num > MAX_RETURNS) {
            WasmEdge_StoreDelete(store_cxt);
            WasmEdge_VMDelete(vm_cxt);
            ereport(ERROR, (errmsg("wasm_executor: func %s has more than 1 return value which not support", tmp_buffer)));
        }

        WasmFuncInfo *funcinfo = new(std::nothrow)WasmFuncInfo();

        enum WasmEdge_ValType param_buffer[10]; // we allow max 10 parameters
        param_nums = WasmEdge_FunctionTypeGetParameters(func_type_list[i], param_buffer, 10);
        for (unsigned int j = 0; j < param_nums; ++j) {
            if (param_buffer[j] == WasmEdge_ValType_I32) {
                funcinfo->inputs.push_back("integer");
            } else if (param_buffer[j] == WasmEdge_ValType_I64) {
                funcinfo->inputs.push_back("bigint");
            } else {
                WasmEdge_StoreDelete(store_cxt);
                WasmEdge_VMDelete(vm_cxt);
                ereport(ERROR, (errmsg("wasm_executor: not support the value type(%d) for now", param_buffer[j])));
            }
        }
        
        return_num = WasmEdge_FunctionTypeGetReturns(func_type_list[i], param_buffer, 10);
        if (param_buffer[0] == WasmEdge_ValType_I32) {
            funcinfo->outputs = "integer";
        } else if (param_buffer[0] == WasmEdge_ValType_I64) {
            funcinfo->outputs = "bigint";
        } else {
            WasmEdge_StoreDelete(store_cxt);
            WasmEdge_VMDelete(vm_cxt);
            ereport(ERROR, (errmsg("wasm_executor: not support the value type(%d) for now", param_buffer[0])));
        }

        funcinfo->funcname = std::string(tmp_buffer, func_name_len);
        functions->push_back(funcinfo);
    }

    WasmEdge_StoreDelete(store_cxt);
    WasmEdge_VMDelete(vm_cxt);

    inter_call_data->currindex = functions->begin();
    inter_call_data->lastindex = functions->end();
    elog(DEBUG1, "wasm_executor:init exported func info for instanceid %ld", instanceid); 
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

    std::string wasm_file = find_wasm_file(uuid);
    if (wasm_file != "") {
        ereport(NOTICE, (errmsg("wasm_executor: instance already created for %s", filepath)));
        return UInt32GetDatum(uuid);
    }
    
    WasmEdge_ConfigureContext *config_context = WasmEdge_ConfigureCreate();
    WasmEdge_ConfigureAddHostRegistration(config_context, WasmEdge_HostRegistration_Wasi);
    WasmEdge_VMContext *vm_cxt = WasmEdge_VMCreate(config_context, NULL);

    WasmEdge_Result result = WasmEdge_VMLoadWasmFromFile(vm_cxt, filepath);
    if (!WasmEdge_ResultOK(result)) {
        WasmEdge_VMDelete(vm_cxt);
        WasmEdge_ConfigureDelete(config_context);
        ereport(ERROR, (errmsg("wasm_executor: failed to load %s", filepath)));
    }

    result = WasmEdge_VMValidate(vm_cxt);
    if (!WasmEdge_ResultOK(result)) {
        WasmEdge_VMDelete(vm_cxt);
        WasmEdge_ConfigureDelete(config_context);
        ereport(ERROR, (errmsg("wasm_executor: wasm file validation failed %s", WasmEdge_ResultGetMessage(result))));
    }
    wasm_file = filepath;
    instances.insert(std::pair<int64, std::string>(uuid, wasm_file));

    WasmEdge_VMDelete(vm_cxt);
    WasmEdge_ConfigureDelete(config_context);
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

        std::string wasm_file = inter_call_data->currindex->second;
        values[0] = Int64GetDatum(inter_call_data->currindex->first);
        values[1] = CStringGetTextDatum(wasm_file.c_str());

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
        std::string inputs;
        for (std::vector<std::string>::iterator curr = funcinfo->inputs.begin(); curr != funcinfo->inputs.end(); curr++) {
            inputs += *curr;
            inputs += ",";
        }
        if (inputs.length() > 0) {
            inputs.pop_back();
        }
        values[1] = CStringGetTextDatum(inputs.c_str());
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
