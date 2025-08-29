

let t: f32 = 0;


//// ECS GET COMPONENT EXAMPLE
// const comp = ECS.getComponent(ComponentType.Transform);
// if (comp && comp.type === "transform") {
//     const transComp = comp as TransformComponent;
//     console.log(`Position: <${transComp.x}, ${transComp.y}>, Scale: <${transComp.sx}, ${transComp.sy}>`);
// }
//////

export function onUpdate(dt: f32): void {
    t += dt;

    if (Inputs.keyIsPressed(Key.A)) {
        console.log("Pressed Key!");
    }
}
