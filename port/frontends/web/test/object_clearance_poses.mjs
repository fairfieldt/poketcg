// Check actual scripted presentation frames, including interpolated corners.
import assert from 'node:assert/strict';

export function checkClearancePoses(world, view) {
    if (!view.map || !view.presentation) return;
    const map = world.maps[view.map];
    let path = map.scene;
    for (const event of world.events)
        if (event.map === view.map && view.mapPatch[event.event]) path = event.scene;
    const props = world.meshes[path.split('/').at(-1).replace('.l64', '')].props;
    const actors = [{ name: 'player', id: 0, pose: view.presentation.player }];
    for (const npc of view.npcs) {
        if (!npc.visible || npc.id >= 103 && npc.id <= 109) continue;
        const pose = view.presentation.npcs.find(actor => actor.id === npc.id)?.pose;
        // Ronald enters along the hidden backstage row, then the north door.
        if (view.map === 32 && npc.id === 1 && pose?.z < 1) continue;
        actors.push({ name: `NPC ${npc.id}`, id: npc.id, pose });
    }
    for (const { name, id, pose } of actors) {
        if (!pose || pose.x < 0 || pose.z < 0 || pose.x >= map.width || pose.z >= map.height) continue;
        for (const prop of props) {
            if (prop.solid === false || pose.floor && prop.supports_npcs?.includes(id)) continue;
            const [left, north, right, south] = prop.solid_bounds;
            const distance = Math.hypot(Math.max(left - pose.x, pose.x - right, 0),
                Math.max(north - pose.z, pose.z - south, 0));
            assert(distance >= .6498,
                `${map.slug}: ${name} at ${pose.x},${pose.z} intersects ${prop.kind} ${prop.solid_bounds} (${distance})`);
        }
    }
}
