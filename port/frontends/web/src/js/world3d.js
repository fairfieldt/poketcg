/* Shared N64 scene assets, drawn by WebGL. L643 is deliberately kept as the
 * interchange format so browser and cartridge builds use the same geometry,
 * texture coordinates, colors, rigid joints, and character aliases. */

const DEG = Math.PI / 180;
const AMY_LYING = 0x2e;
const NPC_TORCH = 0x67, NPC_LEGENDARY_CARD_RIGHT_SPARK = 0x6d;
const IDENTITY = new Float32Array([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]);

/** Parse a complete big-endian native scene without any browser dependency. */
export function parseL643(input) {
    const bytes = input instanceof Uint8Array ? input : new Uint8Array(input);
    const data = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    const need = (offset, length) => {
        if (offset < 0 || length < 0 || offset + length > bytes.length) throw new Error('Truncated L643 scene');
    };
    need(0, 8);
    if (String.fromCharCode(...bytes.subarray(0, 4)) !== 'L643') throw new Error('Unsupported scene format');
    const count = data.getUint16(4), textureCount = data.getUint16(6);
    if (count > 1024 || textureCount > 512) throw new Error('Invalid scene counts');
    let offset = 8;
    const textures = [];
    for (let i = 0; i < textureCount; i++, offset += 32) {
        need(offset, 32);
        const field = bytes.subarray(offset, offset + 32), end = field.indexOf(0);
        if (end < 0) throw new Error('Unterminated scene texture');
        const name = String.fromCharCode(...field.subarray(0, end));
        if (!/^[A-Za-z0-9_-]+\.sprite$/.test(name)) throw new Error('Invalid scene texture path');
        textures.push(name.replace(/\.sprite$/, '.png'));
    }
    const meshes = [];
    for (let i = 0; i < count; i++) {
        need(offset, 28);
        const model = data.getUint16(offset), part = data.getUint16(offset + 2);
        const texture = data.getInt16(offset + 4), vertexCount = data.getUint32(offset + 8);
        const indexCount = data.getUint32(offset + 12);
        const pivot = [0, 1, 2].map(n => data.getFloat32(offset + 16 + n * 4));
        offset += 28;
        if (!vertexCount || vertexCount > 65535 || indexCount % 3 || texture < -1 || texture >= textureCount) throw new Error('Invalid scene batch');
        if (pivot.some(n => !Number.isFinite(n))) throw new Error('Invalid scene pivot');
        need(offset, vertexCount * 24 + indexCount * 2);
        const vertices = new ArrayBuffer(vertexCount * 24), native = new DataView(vertices);
        let repeatS = false, repeatT = false;
        for (let v = 0; v < vertexCount; v++) {
            for (let component = 0; component < 5; component++) {
                const value = data.getFloat32(offset + v * 24 + component * 4);
                if (!Number.isFinite(value)) throw new Error('Invalid scene vertex');
                native.setFloat32(v * 24 + component * 4, value, true);
                if (value < -.000001 || value > 1.000001) {
                    if (component === 3) repeatS = true;
                    if (component === 4) repeatT = true;
                }
            }
            native.setUint32(v * 24 + 20, data.getUint32(offset + v * 24 + 20));
        }
        offset += vertexCount * 24;
        const indices = new Uint16Array(indexCount);
        for (let j = 0; j < indexCount; j++) {
            indices[j] = data.getUint16(offset + j * 2);
            if (indices[j] >= vertexCount) throw new Error('Scene index out of range');
        }
        offset += indexCount * 2;
        offset = (offset + 3) & ~3;
        meshes.push({ model, part, texture, vertexCount, indexCount, pivot, vertices, indices, repeatS, repeatT });
    }
    if (offset !== bytes.length) throw new Error('Trailing scene bytes');
    return { textures, meshes };
}

function multiply(a, b) {
    const out = new Float32Array(16);
    for (let column = 0; column < 4; column++) for (let row = 0; row < 4; row++) {
        let value = 0;
        for (let k = 0; k < 4; k++) value += a[k * 4 + row] * b[column * 4 + k];
        out[column * 4 + row] = value;
    }
    return out;
}
function translation(x, y, z) { const m = IDENTITY.slice(); m[12] = x; m[13] = y; m[14] = z; return m; }
function scale(x, y = x, z = x) { const m = IDENTITY.slice(); m[0] = x; m[5] = y; m[10] = z; return m; }
function rotationX(a) { const c = Math.cos(a), s = Math.sin(a); return new Float32Array([1, 0, 0, 0, 0, c, s, 0, 0, -s, c, 0, 0, 0, 0, 1]); }
function rotationY(a) { const c = Math.cos(a), s = Math.sin(a); return new Float32Array([c, 0, -s, 0, 0, 1, 0, 0, s, 0, c, 0, 0, 0, 0, 1]); }
function perspective(fov, aspect, near = .15, far = 160) {
    const f = 1 / Math.tan(fov * DEG / 2), inverse = 1 / (near - far);
    return new Float32Array([f / aspect, 0, 0, 0, 0, f, 0, 0, 0, 0, (far + near) * inverse, -1, 0, 0, 2 * far * near * inverse, 0]);
}
function lookAt(eye, target) {
    const normalize = a => { const n = Math.hypot(...a) || 1; return a.map(v => v / n); };
    const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
    const dot = (a, b) => a.reduce((sum, v, i) => sum + v * b[i], 0);
    const z = normalize(eye.map((v, i) => v - target[i])), x = normalize(cross([0, 1, 0], z)), y = cross(z, x);
    return new Float32Array([x[0], y[0], z[0], 0, x[1], y[1], z[1], 0, x[2], y[2], z[2], 0, -dot(x, eye), -dot(y, eye), -dot(z, eye), 1]);
}
const coordinate = n => n >= 240 ? n - 256 : n;
const heading = direction => 180 - ((direction || 0) & 3) * 90;
const angleDelta = (to, from) => (((to - from + 180) % 360) + 360) % 360 - 180;
const visible = npc => npc.visible !== false && npc.visible !== 0;

const VERTEX_SHADER = `
attribute vec3 aPosition;
attribute vec2 aUV;
attribute vec4 aColor;
uniform mat4 uTransform;
varying vec2 vUV;
varying vec4 vColor;
void main() { gl_Position=uTransform*vec4(aPosition,1.0); vUV=aUV; vColor=aColor; }
`;
const FRAGMENT_SHADER = `
precision mediump float;
uniform sampler2D uTexture;
uniform float uTextured;
varying vec2 vUV;
varying vec4 vColor;
void main() {
    vec4 color=vColor*mix(vec4(1.0),texture2D(uTexture,vUV),uTextured);
    if(color.a<0.02)discard;
    gl_FragColor=color;
}
`;

/**
 * const world = await new World3D(canvas).load();
 * await world.update(view); world.draw(view, performance.now());
 * Native presentation poses/cameras are preferred when view.presentation exists.
 */
export class World3D {
    constructor(canvas, options = {}) {
        this.canvas = canvas;
        this.options = options;
        this.baseURL = new URL(options.baseURL || 'world/', globalThis.document?.baseURI || globalThis.location?.href || 'http://localhost/');
        this.gl = canvas.getContext('webgl', { antialias: true, alpha: false, premultipliedAlpha: false, powerPreference: 'high-performance' });
        if (!this.gl) throw new Error('WebGL is unavailable.');
        this.scenes = new Map(); this.textures = new Map(); this.poses = new Map();
        this.downloads = 0; this.downloadWaiters = [];
        this.loadedMap = -1; this.generation = 0; this.roomScenes = [];
        this.lost = false; this.lastTime = 0; this.view = null; this.worldRoute = [];
        this._initGL();
        this._lost = event => { event.preventDefault(); this.lost = true; };
        this._restored = async () => {
            this.lost = false; this.scenes.clear(); this.textures.clear(); this._initGL();
            try { await this._loadActors(); if (this.view) await this.update(this.view); }
            catch (error) { this.error = error; }
        };
        canvas.addEventListener('webglcontextlost', this._lost);
        canvas.addEventListener('webglcontextrestored', this._restored);
    }
    _initGL() {
        const gl = this.gl;
        this.repeatableTextures = new WeakSet(); this.textureWrapping = new WeakMap();
        const compile = (type, source) => {
            const shader = gl.createShader(type); gl.shaderSource(shader, source); gl.compileShader(shader);
            if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(shader));
            return shader;
        };
        const vertex = compile(gl.VERTEX_SHADER, VERTEX_SHADER), fragment = compile(gl.FRAGMENT_SHADER, FRAGMENT_SHADER);
        this.program = gl.createProgram(); gl.attachShader(this.program, vertex); gl.attachShader(this.program, fragment); gl.linkProgram(this.program);
        gl.deleteShader(vertex); gl.deleteShader(fragment);
        if (!gl.getProgramParameter(this.program, gl.LINK_STATUS)) throw new Error(gl.getProgramInfoLog(this.program));
        this.attributes = Object.fromEntries(['Position', 'UV', 'Color'].map(key => [key, gl.getAttribLocation(this.program, `a${key}`)]));
        this.uniforms = Object.fromEntries(['Transform', 'Texture', 'Textured'].map(key => [key, gl.getUniformLocation(this.program, `u${key}`)]));
        this.white = gl.createTexture(); gl.bindTexture(gl.TEXTURE_2D, this.white);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 1, 1, 0, gl.RGBA, gl.UNSIGNED_BYTE, new Uint8Array([255, 255, 255, 255]));
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
        // Same small floor fan as the cartridge; it never follows gait bob.
        const ring = [[1, 0], [.8660254, .5], [.5, .8660254], [0, 1], [-.5, .8660254], [-.8660254, .5], [-1, 0], [-.8660254, -.5], [-.5, -.8660254], [0, -1], [.5, -.8660254], [.8660254, -.5], [1, 0]];
        const shadowVertices = new ArrayBuffer(14 * 24), shadowData = new DataView(shadowVertices);
        const shadowVertex = (index, x, z, alpha) => {
            const offset = index * 24;
            shadowData.setFloat32(offset, x * .56, true); shadowData.setFloat32(offset + 4, .045, true); shadowData.setFloat32(offset + 8, z * .40, true);
            [16, 22, 32, alpha].forEach((value, component) => shadowData.setUint8(offset + 20 + component, value));
        };
        shadowVertex(0, 0, 0, 64); ring.forEach(([x, z], index) => shadowVertex(index + 1, x, z, 0));
        const shadowIndices = new Uint16Array(36);
        for (let i = 0; i < 12; i++) shadowIndices.set([0, i + 1, i + 2], i * 3);
        this.contactShadow = { texture: -1, indexCount: shadowIndices.length, vertexBuffer: gl.createBuffer(), indexBuffer: gl.createBuffer() };
        gl.bindBuffer(gl.ARRAY_BUFFER, this.contactShadow.vertexBuffer); gl.bufferData(gl.ARRAY_BUFFER, shadowVertices, gl.STATIC_DRAW);
        gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, this.contactShadow.indexBuffer); gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, shadowIndices, gl.STATIC_DRAW);
        this.anisotropy = gl.getExtension('EXT_texture_filter_anisotropic') || gl.getExtension('WEBKIT_EXT_texture_filter_anisotropic');
    }
    async load() {
        this.metadata = await this._request('world.json', 'json');
        if (this.metadata.format !== 'L643' || this.metadata.maps.length !== 34) throw new Error('Unsupported world assets');
        this.modelPaths = new Map((this.metadata.individual_characters || []).map(model => [model.model, model.file]));
        await this._loadActors();
        return this;
    }
    async _loadActors() {
        this.fallback = await this._scene(this.metadata.shared?.actors || 'actors.l64');
        this.playerModel = this.metadata.player_model || 1;
        await this._scene(this.modelPaths.get(this.playerModel) || 'actors.l64');
    }
    _url(path) { return new URL(String(path).replace(/^rom:\/world\//, ''), this.baseURL); }
    async _request(path, kind) {
        // A room may reference ninety tiny textures. Bound the actual body
        // downloads so simple static servers are not flooded with connections.
        if (this.downloads < 4) this.downloads++;
        else await new Promise(resolve => this.downloadWaiters.push(resolve));
        try {
            const response = await fetch(this._url(path));
            if (!response.ok) throw new Error(`Asset could not be loaded (${response.status}): ${path}`);
            return await response[kind]();
        } finally {
            const next = this.downloadWaiters.shift();
            if (next) next(); else this.downloads--;
        }
    }
    async _texture(name) {
        if (!this.textures.has(name)) {
            const promise = (async () => {
                const blob = await this._request(`textures/${name}`, 'blob');
                let image;
                if (globalThis.createImageBitmap) image = await createImageBitmap(blob, { premultiplyAlpha: 'none', colorSpaceConversion: 'none' });
                else image = await new Promise((resolve, reject) => {
                    const objectURL = URL.createObjectURL(blob), img = new Image();
                    img.onload = () => { URL.revokeObjectURL(objectURL); resolve(img); };
                    img.onerror = () => { URL.revokeObjectURL(objectURL); reject(new Error(`Invalid texture: ${name}`)); };
                    img.src = objectURL;
                });
                const gl = this.gl, texture = gl.createTexture(); gl.bindTexture(gl.TEXTURE_2D, texture);
                gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false); gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, false);
                gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, image);
                gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
                gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
                gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
                const powerOfTwo = n => (n & (n - 1)) === 0;
                this.textureWrapping.set(texture, [false, false]);
                if (powerOfTwo(image.width) && powerOfTwo(image.height)) {
                    this.repeatableTextures.add(texture);
                    gl.generateMipmap(gl.TEXTURE_2D); gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR_MIPMAP_LINEAR);
                    if (this.anisotropy) gl.texParameterf(gl.TEXTURE_2D, this.anisotropy.TEXTURE_MAX_ANISOTROPY_EXT, Math.min(4, gl.getParameter(this.anisotropy.MAX_TEXTURE_MAX_ANISOTROPY_EXT)));
                } else gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
                image.close?.();
                return texture;
            })();
            this.textures.set(name, promise);
            promise.catch(() => this.textures.delete(name));
        }
        return this.textures.get(name);
    }
    async _scene(path) {
        const key = String(path).replace(/^rom:\/world\//, '');
        if (!this.scenes.has(key)) {
            const promise = (async () => {
                const source = parseL643(await this._request(key, 'arrayBuffer'));
                const textures = await Promise.all(source.textures.map(name => this._texture(name)));
                const gl = this.gl;
                const meshes = source.meshes.map(mesh => {
                    const vertexBuffer = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, vertexBuffer); gl.bufferData(gl.ARRAY_BUFFER, mesh.vertices, gl.STATIC_DRAW);
                    const indexBuffer = gl.createBuffer(); gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, indexBuffer); gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, mesh.indices, gl.STATIC_DRAW);
                    return { ...mesh, vertices: undefined, indices: undefined, vertexBuffer, indexBuffer };
                });
                return { meshes, textures, path: key };
            })();
            this.scenes.set(key, promise);
            promise.catch(() => this.scenes.delete(key));
        }
        return this.scenes.get(key);
    }
    _npcModel(npc) {
        const id = npc.id ?? npc.npc;
        const poseModel = this.metadata.npc_pose_models?.[id];
        if ((npc.animation ?? npc.anim) === AMY_LYING && poseModel) return poseModel;
        return this.metadata.npc_models[id] || this.metadata.fallback_npc_models?.[id] || 2;
    }
    _modelPath(model, map) {
        return this.metadata.map_actor_scenes?.find(entry => entry.map === map && entry.model === model)?.scene
            || this.modelPaths.get(model) || 'actors.l64';
    }
    _roomPaths(view) {
        const map = this.metadata.maps[view.map];
        if (!map) throw new Error('Invalid map');
        const patch = view.mapPatch || view.map_patch || [];
        let scene = map.scene;
        if (view.map === 2 && this.metadata.switch_base) scene = this.metadata.switch_base;
        else for (const event of this.metadata.events || []) if (event.map === view.map && patch[event.event]) scene = event.scene;
        const paths = [scene];
        if (map.overlay) paths.push(map.overlay);
        if (view.map === 2) for (const panel of this.metadata.switches || []) paths.push(patch[panel.event] ? panel.on : panel.off);
        return paths;
    }
    async update(view) {
        if (!this.metadata) throw new Error('Load world assets first');
        const generation = ++this.generation, changedMap = this.loadedMap !== view.map;
        const paths = this._roomPaths(view);
        const models = new Set([this.playerModel, ...((view.npcs || []).filter(visible).map(npc => this._npcModel(npc)))]);
        const [rooms, actors] = await Promise.all([
            Promise.all(paths.map(path => this._scene(path))),
            Promise.all([...models].map(async model => [model, await this._scene(this._modelPath(model, view.map))]))
        ]);
        if (generation !== this.generation) return;
        this.roomScenes = rooms; this.actorScenes = new Map(actors); this.loadedMap = view.map;
        if (changedMap) { this.poses.clear(); this.worldRoute = []; this.worldLocation = view.overworldSelection || view.overworld_selection || 1; }
        this._syncPoses(view, performance.now(), changedMap);
        this.view = view;
        this.draw(view);
    }
    _syncPoses(view, now, snap) {
        if (view.presentation) return;
        const running = !!view.running, scripted = view.scriptOpcode === 0x1e;
        const duration = (scripted ? (view.scriptSpeed || 1) > 1 : running) ? 400 / 3 : 800 / 3;
        const sync = (key, x, z, direction, time = duration) => {
            const targetHeading = heading(direction), old = this.poses.get(key);
            x = coordinate(x); z = coordinate(z);
            if (!old || snap || Math.hypot(x - old.x, z - old.z) > 5) {
                this.poses.set(key, { x, z, heading: targetHeading, targetX: x, targetZ: z, targetHeading, startX: x, startZ: z, start: now, duration: 0, phase: 0, gait: 0, bob: 0, lean: 0 });
            } else if (old.targetX !== x || old.targetZ !== z || old.targetHeading !== targetHeading) {
                old.startX = old.x; old.startZ = old.z; old.start = now; old.duration = time;
                old.targetX = x; old.targetZ = z; old.targetHeading = targetHeading; old.running = running;
            }
        };
        sync('player', view.x, view.y ?? view.z, view.direction);
        for (const npc of view.npcs || []) if (visible(npc)) sync(`npc:${npc.id ?? npc.npc}`, npc.x, npc.y ?? npc.z, npc.direction ?? npc.dir, 800 / 3);
        const node = this.metadata.overworld_nodes[(view.overworldSelection || view.overworld_selection || 1) - 1];
        if (node) {
            const pose = this.poses.get('cursor');
            if (!pose || pose.x !== node.marker_x || pose.z !== node.marker_z) this.poses.set('cursor', { x: node.marker_x, z: node.marker_z, heading: 0, gait: 0, bob: 0, lean: 0 });
            if (!this.poses.has('world')) this.poses.set('world', { ...this.poses.get('cursor'), phase: 0 });
        }
    }
    /** Queue source island waypoints; mini Mark moves independently of the cursor. */
    travelWorld(points, destination, options = {}) {
        const factor = options.pixels ? 1 / 8 : 1, bias = options.pixels ? 1 : 0;
        this.worldRoute = points.map(point => ({ x: point[0] * factor + bias, z: point[1] * factor + bias }));
        this.worldLocation = destination;
    }
    _step(now, dt) {
        if (this.view.presentation) return;
        for (const pose of this.poses.values()) {
            if (pose.targetX === undefined) continue;
            const previousX = pose.x, previousZ = pose.z;
            const t = pose.duration ? Math.min(1, Math.max(0, (now - pose.start) / pose.duration)) : 1;
            pose.x = pose.startX + (pose.targetX - pose.startX) * t;
            pose.z = pose.startZ + (pose.targetZ - pose.startZ) * t;
            pose.heading += Math.max(-dt * 1080, Math.min(dt * 1080, angleDelta(pose.targetHeading, pose.heading)));
            const distance = Math.hypot(pose.x - previousX, pose.z - previousZ);
            if (distance > .0001) {
                pose.phase = (pose.phase + distance * Math.PI) % (2 * Math.PI); pose.gait = pose.phase;
                pose.bob = Math.abs(Math.sin(pose.phase)) * (pose.running ? .065 : .035); pose.lean = pose.running ? 6 : 0;
            } else { pose.gait = 0; pose.bob = 0; pose.lean = 0; }
        }
        const world = this.poses.get('world');
        if (world && this.worldRoute.length) {
            const target = this.worldRoute[0], dx = target.x - world.x, dz = target.z - world.z, distance = Math.hypot(dx, dz), step = dt * 15;
            if (distance <= step) { world.x = target.x; world.z = target.z; this.worldRoute.shift(); }
            else { world.x += dx * step / distance; world.z += dz * step / distance; }
            world.heading = Math.atan2(dx, dz) / DEG; world.phase = (world.phase + Math.min(step, distance) * Math.PI) % (Math.PI * 2); world.gait = world.phase;
            world.bob = Math.abs(Math.sin(world.phase)) * .035;
        } else if (world) { world.gait = 0; world.bob = 0; }
    }
    _pose(key, view) {
        const presentation = view.presentation;
        if (presentation) {
            if (key === 'player' || key === 'world' || key === 'cursor') {
                const pose = presentation[key]?.pose || presentation[key];
                return key === 'cursor' ? pose : this._idlePose(pose, 0);
            }
            const id = Number(key.slice(4));
            const npc = Array.isArray(presentation.npcs) ? presentation.npcs.find(npc => npc.id === id) : presentation.npcs?.[id];
            return this._idlePose(npc?.pose || npc, id);
        }
        return this.poses.get(key);
    }
    _idlePose(pose, id) {
        // Host callbacks retain canonical movement poses. Between callbacks,
        // continue the same 30 Hz, 0.008-unit idle breath in presentation only.
        if (!pose || pose.gait || pose.lean || (id >= NPC_TORCH && id <= NPC_LEGENDARY_CARD_RIGHT_SPARK)) return pose;
        return { ...pose, bob: .008 * Math.sin((this.lastTime || 0) * .003 + id * .7) };
    }
    _begin(camera, clear = [.02, .025, .055, 1]) {
        const gl = this.gl, ratio = Math.min(this.options.pixelRatio || globalThis.devicePixelRatio || 1, this.options.maxPixelRatio || 2);
        const width = Math.max(1, Math.round((this.canvas.clientWidth || 640) * ratio));
        const height = Math.max(1, Math.round((this.canvas.clientHeight || 480) * ratio));
        if (this.canvas.width !== width || this.canvas.height !== height) { this.canvas.width = width; this.canvas.height = height; }
        gl.viewport(0, 0, width, height); gl.clearColor(...clear); gl.clearDepth(1); gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
        gl.enable(gl.DEPTH_TEST); gl.depthFunc(gl.LEQUAL); gl.depthMask(true); gl.disable(gl.CULL_FACE);
        gl.enable(gl.BLEND); gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
        gl.useProgram(this.program); gl.activeTexture(gl.TEXTURE0); gl.uniform1i(this.uniforms.Texture, 0);
        const aspect = width / height;
        let fov = Math.max(20, Math.min(90, camera.fov || 58));
        if (camera.fitAspect !== false && aspect < 4 / 3) fov = Math.atan(Math.tan(fov * DEG / 2) * (4 / 3) / aspect) * 2 / DEG;
        this.viewProjection = multiply(perspective(fov, aspect), lookAt(camera.eye, camera.target));
    }
    _batch(scene, mesh, matrix) {
        const gl = this.gl;
        gl.bindBuffer(gl.ARRAY_BUFFER, mesh.vertexBuffer);
        for (const attribute of Object.values(this.attributes)) gl.enableVertexAttribArray(attribute);
        gl.vertexAttribPointer(this.attributes.Position, 3, gl.FLOAT, false, 24, 0);
        gl.vertexAttribPointer(this.attributes.UV, 2, gl.FLOAT, false, 24, 12);
        gl.vertexAttribPointer(this.attributes.Color, 4, gl.UNSIGNED_BYTE, true, 24, 20);
        gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, mesh.indexBuffer);
        const texture = mesh.texture < 0 ? this.white : scene.textures[mesh.texture];
        gl.bindTexture(gl.TEXTURE_2D, texture);
        if (mesh.texture >= 0) {
            // Merged native wall panels use tiled UVs. Keep face/card atlases
            // clamped, and repeat only the axes that actually leave [0,1].
            const canRepeat = this.repeatableTextures.has(texture);
            const repeatS = canRepeat && mesh.repeatS, repeatT = canRepeat && mesh.repeatT;
            const previous = this.textureWrapping.get(texture);
            if (!previous || previous[0] !== repeatS) gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, repeatS ? gl.REPEAT : gl.CLAMP_TO_EDGE);
            if (!previous || previous[1] !== repeatT) gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, repeatT ? gl.REPEAT : gl.CLAMP_TO_EDGE);
            this.textureWrapping.set(texture, [repeatS, repeatT]);
        }
        gl.uniform1f(this.uniforms.Textured, mesh.texture < 0 ? 0 : 1);
        gl.uniformMatrix4fv(this.uniforms.Transform, false, multiply(this.viewProjection, matrix));
        gl.drawElements(gl.TRIANGLES, mesh.indexCount, gl.UNSIGNED_SHORT, 0);
    }
    _room(scene) {
        for (const mesh of scene.meshes) if (!mesh.model) this._batch(scene, mesh, translation(...mesh.pivot));
    }
    _contactShadows(view) {
        const gl = this.gl;
        // _begin enables source-alpha blending. Test against room depth while
        // leaving it intact for all opaque actors drawn after this pass.
        gl.depthMask(false);
        const draw = pose => { if (pose) this._batch(null, this.contactShadow, translation(pose.x, 0, pose.z)); };
        try {
            draw(this._pose('player', view));
            for (const npc of view.npcs || []) if (visible(npc)) {
                const id = npc.id ?? npc.npc;
                if ((id >= NPC_TORCH && id <= NPC_LEGENDARY_CARD_RIGHT_SPARK) || (npc.animation ?? npc.anim) === AMY_LYING) continue;
                const pose = this._pose(`npc:${id}`, view);
                if (!pose?.floor) draw(pose);
            }
        } finally { gl.depthMask(true); }
    }
    _actor(model, pose, actorScale = 1, floor = 0, tilt = 0) {
        if (!pose) return;
        const scene = this.actorScenes?.get(model) || this.fallback;
        let matrix = translation(pose.x, floor + (pose.floor || 0) + (pose.bob || 0) * actorScale + (tilt ? 1 : 0), pose.z);
        matrix = multiply(matrix, rotationY((pose.heading || 0) * DEG));
        matrix = multiply(matrix, scale(actorScale));
        matrix = multiply(matrix, rotationX(((pose.lean || 0) + tilt) * DEG));
        for (const mesh of scene.meshes) if (mesh.model === model) {
            let rigid = multiply(matrix, translation(...mesh.pivot));
            if (mesh.part) {
                let swing = Math.sin(pose.gait || 0) * 24 * DEG;
                if (mesh.part === 2 || mesh.part === 3) swing = -swing;
                rigid = multiply(rigid, rotationX(swing));
            }
            this._batch(scene, mesh, rigid);
        }
    }
    draw(view = this.view, time = performance.now()) {
        if (this.lost || !this.metadata || !this.roomScenes.length) return false;
        if (view?.map === this.loadedMap) this.view = view;
        view = this.view;
        const now = Number.isFinite(time) ? time : performance.now(), dt = Math.min(.1, Math.max(0, (now - (this.lastTime || now)) / 1000));
        this.lastTime = now; this._step(now, dt);
        const map = this.metadata.maps[view.map];
        const camera = view.presentation?.camera || view.camera || map.camera;
        this._begin(camera, view.map ? [26 / 255, 18 / 255, 14 / 255, 1] : [.02, .025, .055, 1]);
        for (const scene of this.roomScenes) this._room(scene);
        if (!view.map) {
            // Map actors are foreground markers, as in the source sprite layer.
            // Clear terrain depth while retaining their own 3D self-occlusion.
            this.gl.clear(this.gl.DEPTH_BUFFER_BIT);
            this._actor(this.playerModel, this._pose('world', view), .48, .35);
            const cursor = this._pose('cursor', view);
            if (cursor) this._actor(6, { ...cursor, heading: 0, gait: 0, bob: (cursor.bob || 0) + Math.sin(now * .003) * .06 }, .55, 1.8);
        } else {
            this._contactShadows(view);
            for (const npc of view.npcs || []) if (visible(npc)) {
                const id = npc.id ?? npc.npc, model = this._npcModel(npc);
                const tilt = (npc.animation ?? npc.anim) === AMY_LYING && !this.metadata.npc_pose_models?.[id] ? 90 : 0;
                this._actor(model, this._pose(`npc:${id}`, view), 1, 0, tilt);
            }
            this._actor(this.playerModel, this._pose('player', view));
        }
        return true;
    }
    async portrait() {
        if (!this.metadata) await this.load();
        this.portraitScene = await this._scene(this.modelPaths.get(this.playerModel) || 'actors.l64');
        return this;
    }
    drawPortrait(time = performance.now()) {
        if (this.lost || !this.portraitScene) return false;
        const portraitOffset = (this.canvas.clientWidth || this.canvas.width) > (this.canvas.clientHeight || this.canvas.height) ? .65 : 0;
        this._begin({ eye: [portraitOffset, 1.28, 4.7], target: [portraitOffset, 1.08, 0], fov: 34, fitAspect: false }, [.69, .82, .86, 1]);
        const previous = this.actorScenes;
        this.actorScenes = new Map([[this.playerModel, this.portraitScene]]);
        this._actor(this.playerModel, { x: 0, z: 0, heading: -12 + Math.sin(time * .00075) * 5, gait: 0, bob: Math.sin(time * .002) * .012 });
        this.actorScenes = previous;
        return true;
    }
    /** Wait for fallback tweens when the C host suspends at a movement command. */
    async settle() {
        if (this.view?.presentation) return;
        let until = performance.now();
        for (const pose of this.poses.values()) until = Math.max(until, (pose.start || 0) + (pose.duration || 0));
        await new Promise(resolve => setTimeout(resolve, Math.max(0, until - performance.now())));
    }
    dispose() {
        this.generation++;
        this.canvas.removeEventListener('webglcontextlost', this._lost);
        this.canvas.removeEventListener('webglcontextrestored', this._restored);
        const gl = this.gl;
        for (const promise of this.scenes.values()) promise.then(scene => {
            for (const mesh of scene.meshes) { gl.deleteBuffer(mesh.vertexBuffer); gl.deleteBuffer(mesh.indexBuffer); }
        }).catch(() => {});
        for (const promise of this.textures.values()) promise.then(texture => gl.deleteTexture(texture)).catch(() => {});
        gl.deleteBuffer(this.contactShadow.vertexBuffer); gl.deleteBuffer(this.contactShadow.indexBuffer);
        gl.deleteTexture(this.white); gl.deleteProgram(this.program);
        this.scenes.clear(); this.textures.clear(); this.roomScenes = [];
    }
}
