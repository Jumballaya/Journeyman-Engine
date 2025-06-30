
@external("env", "log")
declare function _log(ptr: i32, len: i32): void;

@external("env", "get_component")
declare function _get_component(entityId: i32, typePtr: i32, typeLen: i32): i32;