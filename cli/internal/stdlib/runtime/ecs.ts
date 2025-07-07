function getComponent(entityId: i32, component: string): i32 {
  const utf8 = String.UTF8.encode(component, true);
  const view = Uint8Array.wrap(utf8);
  return _get_component(entityId, view.dataStart, view.length - 1);
}
