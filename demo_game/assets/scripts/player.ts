

let t: f32 = 0;

export function onUpdate(dt: f32): void {
    t += dt;

    const comp = ECS.getComponent(ComponentType.Transform);

    if (comp && comp.type === "sprite") {
        const spriteComp = comp as SpriteComponent;
        console.log(`Color: rgba(${spriteComp.r}, ${spriteComp.g}, ${spriteComp.b}, ${spriteComp.a})`);
    }

    if (comp && comp.type === "transform") {
        const transComp = comp as TransformComponent;
        console.log(`Position: <${transComp.x}, ${transComp.y}>, Scale: <${transComp.sx}, ${transComp.sy}>`);
    }
}
