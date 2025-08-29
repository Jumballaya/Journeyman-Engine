

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

    const comp = ECS.getComponent(ComponentType.Transform);
    if (!comp || comp.type !== "transform") {
        return;
    }

    const transComp = comp as TransformComponent;
    let updated = false;

    if (Inputs.keyIsDown(Key.A)) {
        transComp.x = transComp.x - 3;
        updated = true;
    }
    if (Inputs.keyIsDown(Key.D)) {
        transComp.x = transComp.x + 3;
        updated = true;
    }
    if (Inputs.keyIsDown(Key.W)) {
        transComp.y = transComp.y + 3;
        updated = true;
    }
    if (Inputs.keyIsDown(Key.S)) {
        transComp.y = transComp.y - 3;
        updated = true;
    }

    if (updated) {
        ECS.updateComponent(ComponentType.Transform, transComp);
    }
}
