#include "LuaInstance.h"

#include "LuaProcessor.h"

#include "Network/FINNetworkComponent.h"

#define INSTANCE "Instance"
#define INSTANCE_REF "InstanceRef"
#define INSTANCE_FUNC "InstanceFunc"
#define INSTANCE_UFUNC "InstanceUFunc"
#define CLASS_INSTANCE "ClassInstance"
#define CLASS_INSTANCE_REF "ClassInstanceRef"
#define CLASS_INSTANCE_FUNC "ClassInstanceFunc"

#define OffsetParam(type, off) (type*)((std::uint64_t)param + off)

namespace FicsItKernel {
	namespace Lua {
		std::map<UClass*, std::vector<std::pair<std::string, LuaLibFunc>>> instanceClasses;
		std::map<UClass*, std::vector<std::pair<std::string, LuaLibClassFunc>>> instanceSubclasses;

		Network::NetworkTrace getObjInstance(lua_State* L, int index, UClass* clazz) {
			if (!lua_istable(L, index)) return Network::NetworkTrace();

			lua_getfield(L, index, "__object");
			auto instance = (LuaInstance*)luaL_checkudata(L, -1, INSTANCE_REF);
			lua_pop(L, 1);
			if (!instance) return Network::NetworkTrace();
			UObject* obj = **instance;
			if (!(obj) || !obj->IsA(clazz)) return Network::NetworkTrace();
			return *instance;
		}

		UClass* getClassInstance(lua_State* L, int index, UClass* clazz) {
			if (!lua_istable(L, index)) return nullptr;
			
			lua_getfield(L, index, "__object");
			auto o = (UClass*)luaL_checkudata(L, -1, CLASS_INSTANCE_REF);
			lua_pop(L, 1);
			if (!o || !o->IsChildOf(clazz)) return nullptr;
			return o;
		}

		int luaInstanceUFuncCall(lua_State* L) {
			int args = lua_gettop(L);

			// get data from closure
			auto& ud = *(LuaInstanceUFunc*)lua_touserdata(L, lua_upvalueindex(1));
			UObject* comp = *ud.first;
			UFunction* func = ud.second;

			// check object validity
			if (!comp) {
				luaL_error(L, "component is invalid");
				return 0;
			}

			// allocate parameter space
			void* params = malloc(func->ParmsSize);
			memset(params, 0, func->ParmsSize);

			// init and set parameter values
			std::string error = "";
			int i = 1;
			for (auto property = TFieldIterator<UProperty>(func); property; ++property) {
				auto flags = property->GetPropertyFlags();
				if (flags & CPF_Parm) {
					property->InitializeValue_InContainer(params);
					if (!(flags & (CPF_OutParm | CPF_ReturnParm))) {
						try {
							luaToProperty(L, *property, params, i++);
						} catch (std::exception e) {
							error = "argument #" + std::to_string(i) + " is not of type " + e.what();
							break;
						}
					}
				}
			}

			// execute native function only if no error
			if (error.length() <= 0) {
				comp->ProcessEvent(func, params);
			}
			
			int retargs = 0;
			// free parameters and eventualy push return values to lua
			for (auto property = TFieldIterator<UProperty>(func); property; ++property) {
				auto flags = property->GetPropertyFlags();
				if (flags & CPF_Parm) {
					if (error.length() <= 0 && (flags & (CPF_OutParm | CPF_ReturnParm))) {
						propertyToLua(L, *property, params, ud.first);
						++retargs;
					}
					property->DestroyValue_InContainer(params);
				}
			}

			free(params);

			if (error.length() > 0) {
				return luaL_error(L, std::string("Error at ").append(std::to_string(i).c_str()).append("# parameter: ").append(error).c_str());
			}

			return retargs;
		}

		int luaInstanceFuncCall(lua_State* L) {
			auto& data = *(LuaInstanceFunc*) lua_touserdata(L, lua_upvalueindex(1));
			auto obj = data.first;
			auto func = data.second;

			if (!*obj) return luaL_error(L, "component is invalid");

			return func(L, lua_gettop(L), obj);
		}

		int luaClassInstanceFuncCall(lua_State* L) {
			int args = lua_gettop(L);

			auto& data = *(LuaClassInstanceFunc*) lua_touserdata(L, lua_upvalueindex(1));
			auto obj = data.first;
			auto func = data.second;
			func(L, lua_gettop(L), obj);
			return luaL_error(L, "invalid native function ptr");
		}

		void addCompFuncs(lua_State* L, Network::NetworkTrace obj) {
			UObject* o = *obj;
			for (auto func = TFieldIterator<UFunction>(o->GetClass()); func; ++func) {
				auto funcName = func->GetName();
				if (!(funcName.RemoveFromStart("netFunc_") && funcName.Len() > 0)) continue;
				auto comp_ud = (LuaInstanceUFunc*) lua_newuserdata(L, sizeof(LuaInstanceUFunc));
				new (comp_ud) LuaInstanceUFunc{obj, *func};
				luaL_setmetatable(L, INSTANCE_UFUNC);
				lua_pushcclosure(L, luaInstanceUFuncCall, 1);
				lua_setfield(L, -2, TCHAR_TO_UTF8(*funcName));
			}
		}

		void addPreFuncs(lua_State* L, Network::NetworkTrace obj) {
			int j = 0;
			for (auto clazz : instanceClasses) {
				if (!obj->IsA(clazz.first)) continue;
				for (auto& func : clazz.second) {
					auto comp_ud = (LuaInstanceFunc*) lua_newuserdata(L, sizeof(LuaInstanceFunc));
					new (comp_ud) LuaInstanceFunc(obj, func.second);
					luaL_setmetatable(L, INSTANCE_FUNC);
					lua_pushcclosure(L, luaInstanceFuncCall, 1);
					lua_setfield(L, -2, func.first.c_str());
				}
			}
		}

		bool newInstance(lua_State* L, Network::NetworkTrace obj) {
			if (!*obj) {
				lua_pushnil(L);
				return false;
			}

			lua_newtable(L);
			luaL_setmetatable(L, INSTANCE);

			auto ud_o = (LuaInstance*)lua_newuserdata(L, sizeof(LuaInstance));
			luaL_setmetatable(L, INSTANCE_REF);
			new (ud_o) LuaInstance(obj);
			lua_setfield(L, -2, "__object");
			addPreFuncs(L, obj);
			addCompFuncs(L, obj);

			UObject* o = *obj;
			if (auto comp = Cast<IFINNetworkComponent>(o)) {
				FGuid id = comp->Execute_GetID(o);
				lua_pushstring(L, TCHAR_TO_UTF8(*id.ToString()));
				lua_setfield(L, -2, "id");
				FString nick = comp->Execute_GetNick(o);
				lua_pushstring(L, TCHAR_TO_UTF8(*nick));
				lua_setfield(L, -2, "nick");

				TSet<UObject*> merged = comp->Execute_GetMerged(o);
				for (auto m : merged) {
					if (!m) continue;
					addPreFuncs(L, obj/m);
					addCompFuncs(L, obj/m);
				}
			}
			return true;
		}

		bool newInstance(lua_State* L, UClass* iClazz) {
			lua_newtable(L);
			luaL_setmetatable(L, CLASS_INSTANCE);

			auto ud_o = (LuaClassInstance*)lua_newuserdata(L, sizeof(LuaClassInstance));
			luaL_setmetatable(L, CLASS_INSTANCE_REF);
			new (ud_o) LuaClassInstance(iClazz);
			lua_setfield(L, -2, "__object");

			int j = 0;
			for (auto clazz : instanceSubclasses) {
				if (iClazz->IsChildOf(clazz.first))
				for (auto func : clazz.second) {
					auto instanceFunc = (LuaClassInstanceFunc*)lua_newuserdata(L, sizeof(LuaClassInstanceFunc));
					new (instanceFunc) LuaClassInstanceFunc{iClazz, func.second};
					luaL_setmetatable(L, CLASS_INSTANCE_FUNC);
					lua_pushcclosure(L, luaClassInstanceFuncCall, 1);
					lua_setfield(L, -2, func.first.c_str());
				}
			}
			return true;
		}

		int luaInstanceEQ(lua_State* L) {
			bool failed = false;
			if (lua_gettop(L) < 2 || !lua_getmetatable(L, 1) || !lua_getmetatable(L, 2)) failed = true;
			luaL_getmetatable(L, INSTANCE);
			if (!failed && (!lua_compare(L, 3, 5, LUA_OPEQ) || !lua_compare(L, 4, 5, LUA_OPEQ))) failed = true;
			if (!failed && (lua_getfield(L, 1, "__object") != LUA_TUSERDATA || lua_getfield(L, 2, "__object") != LUA_TUSERDATA)) failed = true;
			if (failed) {
				lua_pushboolean(L, false);
				return 1;
			}

			const LuaInstance& obj1 = *(LuaInstance*)luaL_checkudata(L, -2, INSTANCE_REF);
			const LuaInstance& obj2 = *(LuaInstance*)luaL_checkudata(L, -1, INSTANCE_REF);
			lua_pushboolean(L, obj1.isEqualObj(obj2));
			return 1;
		}

		int luaInstanceToString(lua_State* L) {
			lua_getfield(L, 1, "__object");
			const LuaInstance& obj = *(LuaInstance*)luaL_checkudata(L, -1, INSTANCE_REF);
			
			if (!*obj) lua_pushstring(L, "Unavailable");
			else {
				UObject* o = *obj;
				auto nick = IFINNetworkComponent::Execute_GetNick(o);
				lua_pushstring(L, (((nick.Len() > 0) ? std::string("\"") + TCHAR_TO_UTF8(*nick) + "\" " : std::string()) + TCHAR_TO_UTF8(*IFINNetworkComponent::Execute_GetID(o).ToString())).c_str());
			}
			return 1;
		}

		static const luaL_Reg luaInstanceLib[] = {
			{"__eq", luaInstanceEQ},
			{"__tostring", luaInstanceToString},
			{NULL, NULL}
		};

		int luaInstanceRefUnpersist(lua_State* L) {
			// get persist helper
			lua_getfield(L, LUA_REGISTRYINDEX, "PersistStorage");
			FLuaProcessorStateStorage& storage = *(FLuaProcessorStateStorage*)lua_touserdata(L, -1);
				
			// read id from upvalue & get ref from helper
			FFINNetworkTrace trace = storage.GetTrace(lua_tointeger(L, lua_upvalueindex(1)));
			
			// push data as InstanceRef
			LuaInstance* inst = (LuaInstance*)lua_newuserdata(L, sizeof(LuaInstance));
			luaL_setmetatable(L, INSTANCE_REF);
			new (inst) LuaInstance(trace);
			return 1;
		}
#pragma optimize("", off)
		int luaInstanceRefPersist(lua_State* L) {
			// get data
			const LuaInstance& obj = *(LuaInstance*)luaL_checkudata(L, -1, INSTANCE_REF);

			// get persist helper
			lua_getfield(L, LUA_REGISTRYINDEX, "PersistHelper");
			FLuaProcessorStateStorage& storage = *(FLuaProcessorStateStorage*)lua_touserdata(L, -1);

			// add trace to helper & push id
			lua_pushinteger(L, storage.Add(obj));

			// create & return closure
			lua_pushcclosure(L, &luaInstanceRefUnpersist, 1);
			return 1;
		}
#pragma optimize("", on)

		static const luaL_Reg luaInstanceRefLib[] = {
			{"__persist", luaInstanceRefPersist},
			{NULL, NULL}
		};

		int luaInstanceFuncUnpersist(lua_State* L) {
			// read data from upvalue & prepare reader
			size_t data_len = 0;
			const char* data_raw = lua_tolstring(L, lua_upvalueindex(1), &data_len);
			TArray<uint8> data = TArray<uint8>((uint8*)data_raw, data_len);
			FMemoryReader reader = FMemoryReader(data);

			// read data
			FWeakObjectPtr ptr;
			//reader << ptr; // TODO: use network trace instead
			// TODO: Read func ref

			// push data as InstanceFunc
			lua_pushnil(L); // TODO: push InstanceFunc & set metatable
			LuaInstanceFunc* func = (LuaInstanceFunc*)lua_newuserdata(L, sizeof(LuaInstanceFunc));
			luaL_setmetatable(L, INSTANCE_FUNC);
			new (func) LuaInstanceFunc();
			return 1;
		}

		int luaInstanceFuncPersist(lua_State* L) {
			const LuaInstanceFunc& func = *(LuaInstanceFunc*)luaL_checkudata(L, 1, INSTANCE_FUNC);

			// prepare data storage and writer
			TArray<uint8> data;
			// FObjectWriter writer = FObjectWriter(data);

			// serialize func
			FWeakObjectPtr ptr = func.first.getUnderlyingPtr().GetEvenIfUnreachable();
			//writer << ptr; // TODO: use network trace instead
			// TODO: write func ref

			// push data as string
			lua_pushlstring(L, (const char*)data.GetData(), data.Num());
			lua_pushcclosure(L, &luaInstanceFuncUnpersist, 1);
			return 1;
		}
		
		int luaInstanceFuncGC(lua_State* L) {
			const LuaInstanceFunc* c = (LuaInstanceFunc*) luaL_checkudata(L, 1, INSTANCE_FUNC);
			c->~LuaInstanceFunc();
			return 0;
		}

		static const luaL_Reg luaInstanceFuncLib[] = {
			{"__persist", luaInstanceFuncPersist},
			{"__gc", luaInstanceFuncGC},
			{NULL,NULL}
		};

		int luaInstanceUFuncUnpersist(lua_State* L) {
			// get data from stack & prepare reader
			size_t data_len = 0;
			const char* data_raw = lua_tolstring(L, lua_upvalueindex(1), &data_len);
			TArray<uint8> data = TArray<uint8>((uint8*)data_raw, data_len);
			FMemoryReader reader = FMemoryReader(data);

			// read data
			FWeakObjectPtr ptr; // TODO: use network trace instead
			//reader << ptr;
			UFunction* func_ptr;
			//reader << func_ptr;

			// push data as InstanceUFunc
			LuaInstanceUFunc* func = (LuaInstanceUFunc*)lua_newuserdata(L, sizeof(LuaInstanceUFunc));
			luaL_setmetatable(L, INSTANCE_UFUNC);
			new (func) LuaInstanceUFunc(Network::NetworkTrace(ptr.GetEvenIfUnreachable()), func_ptr);
			return 1;
		}

		int luaInstanceUFuncPersist(lua_State* L) {
			const LuaInstanceUFunc& func = *(LuaInstanceUFunc*)luaL_checkudata(L, 1, INSTANCE_UFUNC);

			// prepare data storage and writer
			TArray<uint8> data;
			//FObjectWriter writer = FObjectWriter(data);

			// serialize func
			FWeakObjectPtr ptr = func.first.getUnderlyingPtr().GetEvenIfUnreachable();
			//writer << ptr; // TODO: use network trace instead
			// TODO: write func ref

			// push data as string
			lua_pushlstring(L, (const char*)data.GetData(), data.Num());
			lua_pushcclosure(L, &luaInstanceUFuncUnpersist, 1);
			return 1;
		}

		int luaInstanceUFuncGC(lua_State* L) {
			const LuaInstanceUFunc* c = (LuaInstanceUFunc*) luaL_checkudata(L, 1, INSTANCE_UFUNC);
			c->~LuaInstanceUFunc();
			return 0;
		}

		static const luaL_Reg luaInstanceUFuncLib[] = {
			{"__persist", luaInstanceUFuncPersist},
			{"__gc", luaInstanceUFuncGC},
			{NULL,NULL}
		};

		int luaClassInstanceEQ(lua_State* L) {
			bool failed = false;
			if (lua_gettop(L) < 2 || !lua_getmetatable(L, 1) || !lua_getmetatable(L, 2)) failed = true;
			luaL_getmetatable(L, CLASS_INSTANCE);
			if (!failed && (!lua_compare(L, 3, 5, LUA_OPEQ) || !lua_compare(L, 4, 5, LUA_OPEQ))) failed = true;
			if (!failed && (lua_getfield(L, 1, "__object") != LUA_TUSERDATA || lua_getfield(L, 2, "__object") != LUA_TUSERDATA)) failed = true;
			if (failed) {
				lua_pushboolean(L, false);
				return 1;
			}

			const LuaClassInstance& obj1 = *(LuaClassInstance*)luaL_checkudata(L, -2, CLASS_INSTANCE_REF);
			const LuaClassInstance& obj2 = *(LuaClassInstance*)luaL_checkudata(L, -1, CLASS_INSTANCE_REF);
			lua_pushboolean(L, obj1 == obj2);
			return 1;
		}

		int luaClassInstanceToString(lua_State* L) {
			lua_getfield(L, 1, "__object");
			const LuaClassInstance& obj = *(LuaClassInstance*)luaL_checkudata(L, -1, CLASS_INSTANCE_REF);

			if (obj) {
				FString name = obj->GetName();
				lua_pushstring(L, TCHAR_TO_UTF8(*name));
			} else {
				lua_pushstring(L, "None");
			}
			return 1;
		}

		static const luaL_Reg luaClassInstanceLib[] = {
			{"__eq", luaClassInstanceEQ},
			{"__tostring", luaClassInstanceToString},
			{NULL, NULL}
		};

		int luaClassInstanceFuncGC(lua_State* L) {
			auto c = (LuaClassInstanceFunc*) luaL_checkudata(L, 1, CLASS_INSTANCE_FUNC);
			c->~LuaClassInstanceFunc();
			return 0;
		}

		static const luaL_Reg luaClassInstanceFuncLib[] = {
			{"__gc", luaClassInstanceFuncGC},
			{NULL, NULL}
		};

		// metatables

		void setupInstanceSystem(lua_State* L) {
			PersistSetup("Instance", -2);
			
			luaL_newmetatable(L, INSTANCE);
			luaL_setfuncs(L, luaInstanceLib, 0);
			PersistTable("Instance", -1);
			lua_pop(L, 1);

			luaL_newmetatable(L, INSTANCE_REF);
			luaL_setfuncs(L, luaInstanceRefLib, 0);
			PersistTable("InstanceRef", -1);
			lua_pop(L, 1);
			lua_pushcfunction(L, &luaInstanceRefUnpersist);
			PersistValue("InstanceRefUnpersist");

			luaL_newmetatable(L, CLASS_INSTANCE);
			luaL_setfuncs(L, luaClassInstanceLib, 0);
			PersistTable("ClassInstance", -1);
			lua_pop(L, 1);

			luaL_newmetatable(L, CLASS_INSTANCE_REF);
			PersistTable("ClassInstanceRef", -1);
			lua_pop(L, 1);

			luaL_newmetatable(L, INSTANCE_FUNC);
			luaL_setfuncs(L, luaInstanceFuncLib, 0);
			PersistTable("InstanceFunc", -1);
			lua_pop(L, 1);
			lua_pushcfunction(L, luaInstanceFuncUnpersist);
			PersistValue("InstanceFuncUnpersist");

			luaL_newmetatable(L, INSTANCE_UFUNC);
			luaL_setfuncs(L, luaInstanceUFuncLib, 0);
			PersistTable("InstanceUFunc", -1);
			lua_pop(L, 1);
			lua_pushcfunction(L, luaInstanceUFuncUnpersist);
			PersistValue("InstanceUFuncUnpersist");

			lua_pushcfunction(L, luaInstanceFuncCall);
			PersistValue("InstanceFuncCall");
			lua_pushcfunction(L, luaInstanceUFuncCall);
			PersistValue("InstanceUFuncCall");

			luaL_newmetatable(L, CLASS_INSTANCE_FUNC);
			luaL_setfuncs(L, luaClassInstanceFuncLib, 0);
			PersistTable("ClassInstanceFunc", -1);
			lua_pop(L, 1);

			lua_pushcfunction(L, luaClassInstanceFuncCall);
			PersistValue("ClassInstanceFuncCall");
		}
	}
}