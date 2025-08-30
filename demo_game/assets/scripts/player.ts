

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

export function onCollide(entityIndex: i32, entityGeneration: i32): void {
    const comp = ECS.getComponent(ComponentType.Sprite);
    if (!comp || comp.type !== "sprite") {
        return;
    }
    const sprite = comp as SpriteComponent;

    if (sprite.g === 0 && sprite.b === 0) {
        return;
    }

    sprite.g = 0;
    sprite.b = 0;

    ECS.updateComponent(ComponentType.Sprite, sprite);
}