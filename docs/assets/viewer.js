// The assembled clock in 3D: assets/models/clock_tft.glb and clock_oled.glb, made from the case's assembly STEP files
// (hardware/enclosure/step/assembly_*.step, written by enclosure.py --assembly) by scripts/make_site_models.py. Every
// part is a named node, so the case can be taken apart along the clock's depth and each part named under the pointer.
// The colour screen shows the live clock face (tft.js).

import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js';
import { RoomEnvironment } from 'three/addons/environments/RoomEnvironment.js';
import { MeshoptDecoder } from 'three/addons/libs/meshopt_decoder.module.js';
import { Clock } from './tft.js';

const NAMES = [   // node name (start) -> what it is; explode: how far it moves out, in case depths
  ['front_shell', 'The front: the face, the star grille and the window', 1.0],
  ['back_shell', 'The back: the board clicks into it', -0.7],
  ['knob', 'The knob, pressed onto the 5-way switch', 1.45],
  ['speaker', 'The speaker, 40 mm, clipped into the front', 1.0],
  ['tft_glass', 'The 1.54" colour screen', 0.62], ['tft_module', 'The colour screen module', 0.62],
  ['oled_glass', 'The 1.3" OLED', 0.62], ['oled_module', 'The OLED module', 0.62],
  ['tft_female_header', 'The female header on the display', 0.62], ['oled_female_header', 'The female header on the display', 0.62],
  ['tft_header_tails', 'The female header\'s soldered tails', 0.62], ['oled_header_tails', 'The female header\'s soldered tails', 0.62],
  ['tft_male_header', 'The male header on the board', 0], ['oled_male_header', 'The male header on the board', 0],
  ['tft_male_pins', 'The male header\'s pins', 0], ['oled_male_pins', 'The male header\'s pins', 0],
  ['U2_switch', 'The 5-way switch', 0], ['J3_speaker_terminal', 'The speaker terminal (J3)', 0],
  ['J5_relay_terminal', 'The relay terminal (J5)', 0], ['board parts', 'The ESP32-S3 module, the amplifier and the small parts', 0],
  ['board', 'The circuit board, 59.6 x 50 mm', 0],
];
const info = name => NAMES.find(([n]) => name === n);
export const CASE_COLOURS = { ivory: 0xe4d7bb, teal: 0x17594f, black: 0x26292c };

export class ClockViewer {
  constructor(host, opts = {}) {
    this.host = host;
    this.opts = opts;
    this.status = host.querySelector('.v-status');
    this.label = host.querySelector('.v-label');
    this.renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true });
    this.renderer.setPixelRatio(Math.min(2, window.devicePixelRatio || 1));
    this.renderer.outputColorSpace = THREE.SRGBColorSpace;
    this.renderer.toneMapping = THREE.ACESFilmicToneMapping;
    this.renderer.toneMappingExposure = 0.82;
    host.prepend(this.renderer.domElement);
    this.scene = new THREE.Scene();
    const pmrem = new THREE.PMREMGenerator(this.renderer);
    this.scene.environment = pmrem.fromScene(new RoomEnvironment(), 0.04).texture;
    this.scene.environmentIntensity = 0.75;
    const sun = new THREE.DirectionalLight(0xffffff, 1.4); sun.position.set(0.6, 1, 1.2); this.scene.add(sun);
    this.camera = new THREE.PerspectiveCamera(32, 1, 0.001, 100);
    this.controls = new OrbitControls(this.camera, this.renderer.domElement);
    this.controls.enableDamping = true;
    this.controls.addEventListener('change', () => this.render());
    this.loader = new GLTFLoader().setMeshoptDecoder(MeshoptDecoder);
    this.explode = 0;
    this.colour = 'ivory';
    this.ray = new THREE.Raycaster();
    const el = this.renderer.domElement;
    el.addEventListener('pointermove', ev => this.hover(ev));
    el.addEventListener('pointerleave', () => { this.label.style.display = 'none'; });
    new ResizeObserver(() => this.resize()).observe(host);
    this.loop = this.loop.bind(this);
  }

  async load(display = 'tft') {
    this.display = display;
    this.status.textContent = 'Loading the 3D model...';
    this.status.style.display = '';
    let gltf;
    try { gltf = await this.loader.loadAsync(`assets/models/clock_${display}.glb`); }
    catch (e) { this.status.textContent = 'The 3D model could not load here.'; return; }
    if (this.root) this.scene.remove(this.root);
    if (this.screen) { this.scene.remove(this.screen.mesh); this.screen = null; }   // the OLED has no live face
    const root = new THREE.Group();
    const model = gltf.scene;   // in the case's frame, in metres: X right, Y up, Z toward the viewer
    root.add(model);
    this.scene.add(root);
    this.root = root;
    root.updateMatrixWorld(true);
    // the parts: named nodes, each moved out along the clock's depth (world Z)
    this.parts = [];
    model.traverse(o => {
      const i = info(o.name);
      if (i && !this.parts.some(p => p.obj === o)) this.parts.push({ obj: o, out: i[2], base: o.position.clone(), text: i[1] });
      if (o.isMesh) { o.material = o.material.clone(); o.material.roughness = Math.max(0.45, o.material.roughness ?? 0.6); }
    });
    const box = new THREE.Box3().setFromObject(root), size = box.getSize(new THREE.Vector3()), centre = box.getCenter(new THREE.Vector3());
    root.position.sub(centre);
    this.depth = size.z; this.size = size;
    // each part's offset in its parent's frame for a world +Z step
    root.updateMatrixWorld(true);
    for (const p of this.parts) {
      const inv = new THREE.Matrix4().copy(p.obj.parent.matrixWorld).invert();
      const a = new THREE.Vector3(0, 0, 0).applyMatrix4(inv), b = new THREE.Vector3(0, 0, 1).applyMatrix4(inv);
      p.dir = b.sub(a);
    }
    this.paintCase(this.colour);
    if (display === 'tft') this.addScreen(); else this.screen = null;
    const r = Math.max(size.x, size.y) * 1.25;
    this.camera.near = r / 200; this.camera.far = r * 40; this.camera.updateProjectionMatrix();
    this.camera.position.set(r * 0.72, r * 0.3, r * 1.45);
    this.controls.target.set(0, 0, 0);
    this.controls.minDistance = r * 0.6; this.controls.maxDistance = r * 6;
    this.controls.update();
    this.setExplode(this.explode);
    this.status.style.display = 'none';
    this.resize();
    if (!this.running) { this.running = true; requestAnimationFrame(this.loop); }
  }

  /** The live clock face on the colour screen's glass (its visible area, 27.72 mm, 1.55 mm above the glass's middle). */
  addScreen() {
    const glass = this.parts.find(p => p.obj.name === 'tft_glass');
    if (!glass) return;
    const gb = new THREE.Box3().setFromObject(glass.obj), gs = gb.getSize(new THREE.Vector3()), gc = gb.getCenter(new THREE.Vector3());
    const mm = gs.x / 31.52;
    if (!this.clockCanvas) {
      this.clockCanvas = Object.assign(document.createElement('canvas'), { width: 480, height: 480 });
      Object.assign(this.clockCanvas.style, { position: 'absolute', left: '-9999px', width: '480px', height: '480px' });
      document.body.appendChild(this.clockCanvas);
      this.clock = new Clock(this.clockCanvas, {});
      this.clock.init(this.opts.mosque);
      this.tex = new THREE.CanvasTexture(this.clockCanvas);
      this.tex.colorSpace = THREE.SRGBColorSpace;
      this.tex.anisotropy = 4;
    }
    const plane = new THREE.Mesh(new THREE.PlaneGeometry(27.72 * mm, 27.72 * mm), new THREE.MeshBasicMaterial({ map: this.tex, toneMapped: false }));
    plane.position.set(gc.x, gc.y + 1.55 * mm, gb.max.z + 0.02 * mm);
    this.scene.add(plane);
    this.screen = { mesh: plane, base: plane.position.clone(), out: 0.62 };
  }

  paintCase(colour) {
    this.colour = colour;
    if (!this.parts) return;
    for (const p of this.parts) {
      if (p.obj.name !== 'front_shell' && p.obj.name !== 'back_shell') continue;
      p.obj.traverse(o => { if (o.isMesh) { o.material.color.setHex(CASE_COLOURS[colour]); o.material.metalness = 0; o.material.roughness = 0.7; } });
    }
    this.render();
  }

  setExplode(t) {
    this.explode = t;
    if (!this.parts) return;
    const d = (this.depth || 1) * 1.6 * t;
    for (const p of this.parts) p.obj.position.copy(p.base).addScaledVector(p.dir, d * p.out);
    if (this.screen) this.screen.mesh.position.copy(this.screen.base).add(new THREE.Vector3(0, 0, d * this.screen.out));
    this.render();
  }

  hover(ev) {
    if (!this.root) return;
    const r = this.renderer.domElement.getBoundingClientRect();
    const p = new THREE.Vector2(((ev.clientX - r.left) / r.width) * 2 - 1, -((ev.clientY - r.top) / r.height) * 2 + 1);
    this.ray.setFromCamera(p, this.camera);
    const hit = this.ray.intersectObject(this.root, true)[0];
    let o = hit && hit.object, text = null;
    while (o && !text) { const i = info(o.name); if (i) text = i[1]; o = o.parent; }
    if (hit && !text && this.screen && hit.object === this.screen.mesh) text = 'The live clock face';
    if (!text) { this.label.style.display = 'none'; return; }
    this.label.textContent = text;
    this.label.style.left = (ev.clientX - r.left) + 'px';
    this.label.style.top = (ev.clientY - r.top) + 'px';
    this.label.style.display = 'block';
  }

  resize() {
    const w = this.host.clientWidth, h = this.host.clientHeight;
    if (!w || !h) return;
    this.renderer.setSize(w, h, false);
    this.camera.aspect = w / h; this.camera.updateProjectionMatrix();
    this.render();
  }
  render() { this.dirty = true; }
  loop(t) {
    requestAnimationFrame(this.loop);
    if (this.controls.update()) this.dirty = true;
    if (this.tex && this.screen && (!this.lastTex || t - this.lastTex > 250)) { this.lastTex = t; this.tex.needsUpdate = true; this.dirty = true; }
    if (!this.dirty || !this.visible) return;
    this.dirty = false;
    this.renderer.render(this.scene, this.camera);
  }
}
