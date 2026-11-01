/**
 * La scène 3D d'une vue, sur three.js : scène, lumières, environnement, matériaux partagés selon le thème,
 * caméra orbitale qui laisse défiler la page sur un téléphone, retaillage, perte de contexte, libération.
 *
 * three.js n'est chargé qu'à la première vue 3D (`import()` dynamique) : la page et le repli en 2D ne le paient pas.
 *
 * PLUSIEURS CANVAS SUR UNE PAGE : une `Stage3D`, donc un contexte WebGL, par canvas. Chaque vue a sa propre
 * perte de contexte et, hors écran (`animate` de `canvas.ts`), elle ne dessine ni ne coûte rien.
 *
 * UTILISATION
 *
 *   const stage = await createStage3D(canvas, { onLost, onRestored });   // null : rester en 2D
 *   const { THREE, scene } = stage;
 *   const geometry = toGeometry(THREE, new MeshBuilder().cylinder(10, 40).build());   // ou une géométrie three.js
 *   const pistons = new THREE.InstancedMesh(geometry, stage.materials.steel, 12);
 *   scene.add(pistons);
 *   stage.frameOn(0, 0, 0, 200, { yaw: 0.6, pitch: 0.3 });     // ce qu'il faut voir, et la vue de départ
 *
 *   // Dans la boucle d'`animate`, sans allouer :
 *   pistons.setMatrixAt(i, matrix); pistons.instanceMatrix.needsUpdate = true;
 *   stage.setTheme(readTheme(canvas));       // sans effet si le thème n'a pas changé
 *   stage.render();                          // faux si le contexte est perdu : dessiner en 2D
 *
 *   if (stage.busy()) return;                // le GPU n'a pas fini l'image précédente : ne rien préparer
 *   stage.surface({ color, metalness: 0.9 }); // une matière de plus : PBR sur un GPU, matcap sur un rendu logiciel
 *   stage.resetView(); stage.zoomBy(1.2);    // pour des boutons
 *   stage.project(out, x, y, z);             // position à l'écran, pour des étiquettes
 *   disposeObject(group);                    // libère géométries et matériaux non partagés d'un sous-arbre
 *   stage.dispose();                         // rend le contexte au navigateur
 *
 * Toucher : un glissement vertical fait défiler la page, un glissement parti à l'horizontale fait tourner la vue.
 * Clavier, canvas focalisé (lui donner `tabindex="0"`) : flèches, + et -, 0 pour revenir au départ. La molette reste à la page.
 */
import type * as ThreeNamespace from 'three';
import type { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import type { Theme } from './canvas';
import { type MeshData, MeshBuilder, m34, parseColor, triangulate, type Mat34 } from './mesh3d';

export { MeshBuilder, m34, parseColor, triangulate, type Mat34, type MeshData };

export type Three = typeof import('./threeSubset');

export interface WebGLSupport {
  available: boolean;
  /** Rendu sur le processeur : ni ombres, ni antialiasing, un seul pixel par pixel CSS. */
  software: boolean;
  renderer: string;
}

let support: WebGLSupport | null = null;

/** Essaie un contexte sur un canvas jetable, puis le rend. Ne charge pas three.js. */
export function probeWebGL2(fresh = false): WebGLSupport {
  if (support && !fresh) return support;
  support = { available: false, software: false, renderer: '' };
  try {
    const strict = document.createElement('canvas').getContext('webgl2', { failIfMajorPerformanceCaveat: true }) as WebGL2RenderingContext | null;
    const gl = strict ?? (document.createElement('canvas').getContext('webgl2') as WebGL2RenderingContext | null);
    if (gl) {
      // Firefox donne le nom directement, et se plaint de l'extension ; Chromium la réclame.
      const plain = String(gl.getParameter(gl.RENDERER));
      const info = plain.includes('WebKit') ? gl.getExtension('WEBGL_debug_renderer_info') : null;
      const renderer = info ? String(gl.getParameter(info.UNMASKED_RENDERER_WEBGL)) : plain;
      // `?gl=gpu` ou `?gl=soft` forcent une qualité : pour comparer les deux rendus sur une même machine.
      const forced = new URLSearchParams(location.search).get('gl');
      const software = forced === 'gpu' ? false : forced === 'soft' ? true : !strict || /swiftshader|llvmpipe|softpipe|software|basic render/i.test(renderer);
      gl.getExtension('WEBGL_lose_context')?.loseContext();
      support = { available: true, software, renderer };
    }
  } catch {
    // Pas de WebGL2 : la vue reste en 2D.
  }
  return support;
}

interface Modules {
  THREE: Three;
  OrbitControls: typeof OrbitControls;
}

let loading: Promise<Modules> | null = null;

/** Charge three.js une seule fois, à la première vue 3D. */
export function loadThree(): Promise<Modules> {
  loading ??= Promise.all([import('./threeSubset'), import('three/examples/jsm/controls/OrbitControls.js')]).then(([THREE, controls]) => ({ THREE, OrbitControls: controls.OrbitControls }));
  return loading;
}

/** Des tableaux simples vers une géométrie three.js. */
export function toGeometry(THREE: Three, data: MeshData): ThreeNamespace.BufferGeometry {
  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute('position', new THREE.BufferAttribute(Float32Array.from(data.positions), 3));
  if (data.normals) geometry.setAttribute('normal', new THREE.BufferAttribute(Float32Array.from(data.normals), 3));
  geometry.setIndex(new THREE.BufferAttribute(Uint32Array.from(data.indices), 1));
  if (!data.normals) geometry.computeVertexNormals();
  return geometry;
}

/** Les matériaux que partagent les pièces d'une vue. Ils suivent le thème : ne pas les libérer soi-même. */
export interface SharedMaterials {
  /** Acier usiné, poli : arbres, tourillons, soupapes. */
  steel: ThreeNamespace.MeshStandardMaterial;
  /** Acier forgé, plus sombre : bielles, volant, pignons. */
  forged: ThreeNamespace.MeshStandardMaterial;
  /** Fonte ou aluminium brut : pièces de structure. */
  casting: ThreeNamespace.MeshStandardMaterial;
  /** Aluminium clair : pistons, poulies. */
  alloy: ThreeNamespace.MeshStandardMaterial;
  /** Plastique ou caoutchouc noir. */
  rubber: ThreeNamespace.MeshStandardMaterial;
  /** Laiton, cuivre : électrodes, conducteurs. */
  brass: ThreeNamespace.MeshStandardMaterial;
  /** Peinture d'accent du thème. */
  accent: ThreeNamespace.MeshStandardMaterial;
  /** Carter en verre : on voit à travers, les bords restent lisibles. */
  ghost: ThreeNamespace.MeshStandardMaterial;
  /** Ce qui brille tout seul, sans ombre : étincelle, flamme. Chaque exemplaire porte sa couleur. */
  light: ThreeNamespace.MeshBasicMaterial;
}

export interface FrameOptions {
  yaw?: number;
  pitch?: number;
  zoom?: number;
}

export interface Stage3D {
  readonly THREE: Three;
  /**
   * Une matière de plus, partagée ou non : du PBR sur un GPU, une matcap sur un rendu logiciel. Le type reste celui du
   * PBR : changer `metalness` ou `emissive` d'une matcap ne fait simplement rien.
   */
  surface(parameters: ThreeNamespace.MeshStandardMaterialParameters): ThreeNamespace.MeshStandardMaterial;
  readonly renderer: ThreeNamespace.WebGLRenderer;
  readonly scene: ThreeNamespace.Scene;
  readonly camera: ThreeNamespace.PerspectiveCamera;
  readonly controls: OrbitControls;
  readonly materials: SharedMaterials;
  readonly software: boolean;
  /** Vrai entre la perte du contexte et son retour. */
  readonly lost: boolean;
  /** Vrai dès que le visiteur a déplacé la caméra. */
  readonly touched: boolean;
  /** Ce qu'il faut voir en entier : centre et rayon, et la vue de départ. Ne bouge pas une caméra déjà tournée. */
  frameOn(x: number, y: number, z: number, radius: number, view?: FrameOptions): void;
  resetView(): void;
  zoomBy(factor: number): void;
  /** Fait tourner la caméra (radians). */
  orbit(dYaw: number, dPitch: number): void;
  setTheme(theme: Theme): void;
  /**
   * Vrai si le GPU n'a pas fini l'image précédente : autant ne rien préparer. Une machine lente garde ainsi la page
   * à 60 images par seconde, et la vue 3D va à son rythme, sans que le travail s'accumule.
   */
  busy(): boolean;
  /** Dessine, si le contexte est là. */
  render(): boolean;
  /** Position à l'écran, en fraction de la largeur et de la hauteur ; `out[2]` vaut 1 si le point est devant la caméra. */
  project(out: { [i: number]: number }, x: number, y: number, z: number): void;
  dispose(): void;
}

export interface StageOptions {
  /** Pixels par pixel CSS, au plus. Par défaut 2, et 1 sur un rendu logiciel. */
  maxRatio?: number;
  /** Une ombre portée, sur un vrai GPU seulement. */
  shadows?: boolean;
  onLost?: () => void;
  onRestored?: () => void;
}

/** Libère ce qu'un sous-arbre possède. Les matériaux de `keep` (ceux de la scène) restent. */
export function disposeObject(object: ThreeNamespace.Object3D, keep?: SharedMaterials): void {
  const shared = keep ? new Set<unknown>(Object.values(keep)) : null;
  object.traverse((node) => {
    const mesh = node as ThreeNamespace.InstancedMesh;
    mesh.geometry?.dispose();
    (mesh as { dispose?: () => void }).dispose?.();
    const material = mesh.material;
    for (const item of Array.isArray(material) ? material : material ? [material] : []) {
      if (!shared?.has(item)) item.dispose();
    }
  });
}

/**
 * La lumière d'un rendu logiciel : une matcap, une image de sphère éclairée que chaque pixel lit selon sa normale.
 * Le processeur ne peut pas calculer du PBR à 60 images par seconde ; ceci reste net, et coûte vingt fois moins.
 */
function makeMatcap(THREE: Three, shiny: boolean): ThreeNamespace.Texture {
  const size = 128;
  const canvas = document.createElement('canvas');
  canvas.width = size;
  canvas.height = size;
  const c = canvas.getContext('2d')!;
  const middle = size / 2;
  const body = c.createRadialGradient(middle * 0.8, middle * 0.62, 2, middle, middle, middle);
  if (shiny) {
    body.addColorStop(0, '#ffffff');
    body.addColorStop(0.25, '#eceef2');
    body.addColorStop(0.55, '#b4b8c0');
    body.addColorStop(0.78, '#80848b');
    body.addColorStop(0.92, '#c2c7ce');
    body.addColorStop(1, '#6a6d72');
  } else {
    body.addColorStop(0, '#ffffff');
    body.addColorStop(0.6, '#c8c8c8');
    body.addColorStop(1, '#6e6e6e');
  }
  c.fillStyle = body;
  c.fillRect(0, 0, size, size);
  const texture = new THREE.CanvasTexture(canvas);
  texture.colorSpace = THREE.SRGBColorSpace;
  return texture;
}

/** Un atelier sobre, sans texture : deux grands panneaux clairs, assez pour faire lire le métal. */
function makeEnvironment(THREE: Three, renderer: ThreeNamespace.WebGLRenderer): ThreeNamespace.Texture {
  const scene = new THREE.Scene();
  scene.background = new THREE.Color(0x24272c);
  const panel = (color: number, intensity: number, w: number, h: number, x: number, y: number, z: number) => {
    const material = new THREE.MeshBasicMaterial({ color: new THREE.Color(color).multiplyScalar(intensity), side: THREE.DoubleSide });
    const mesh = new THREE.Mesh(new THREE.PlaneGeometry(w, h), material);
    mesh.position.set(x, y, z);
    mesh.lookAt(0, 0, 0);
    scene.add(mesh);
    return mesh;
  };
  panel(0xffffff, 9, 8, 4, 0, 8, 2);
  panel(0xdfe8ff, 3, 3, 8, -8, 2, 3);
  panel(0xfff0dd, 4, 3, 7, 8, 1, 4);
  panel(0x8f9499, 1, 16, 16, 0, -7, 0);
  const pmrem = new THREE.PMREMGenerator(renderer);
  const target = pmrem.fromScene(scene, 0.03);
  pmrem.dispose();
  scene.traverse((node) => {
    const mesh = node as ThreeNamespace.Mesh;
    mesh.geometry?.dispose();
    (mesh.material as ThreeNamespace.Material | undefined)?.dispose();
  });
  return target.texture;
}

const LIGHT_THEME_BACKGROUND = 0.55;
const SOFTWARE_WIDTH = 560;

/** Crée la scène sur ce canvas. `null` : WebGL2 refusé ou three.js injoignable, la vue reste en 2D. */
export async function createStage3D(canvas: HTMLCanvasElement, options: StageOptions = {}): Promise<Stage3D | null> {
  const probe = probeWebGL2();
  if (!probe.available) return null;
  let modules: Modules;
  try {
    modules = await loadThree();
  } catch {
    return null;
  }
  const { THREE, OrbitControls: Controls } = modules;
  const software = probe.software;
  let renderer: ThreeNamespace.WebGLRenderer;
  try {
    renderer = new THREE.WebGLRenderer({ canvas, antialias: !software, alpha: true, powerPreference: 'default', stencil: false });
  } catch {
    return null;
  }
  const ratioCap = options.maxRatio ?? (software ? 1 : 2);
  renderer.setClearColor(0x000000, 0);
  renderer.toneMapping = THREE.ACESFilmicToneMapping;
  renderer.toneMappingExposure = 1;
  const shadows = !!options.shadows && !software;
  renderer.shadowMap.enabled = shadows;
  renderer.shadowMap.type = THREE.PCFSoftShadowMap;

  const scene = new THREE.Scene();
  let environment: ThreeNamespace.Texture | null = software ? null : makeEnvironment(THREE, renderer);
  scene.environment = environment;
  scene.environmentIntensity = 0.55;
  const matcapShiny = software ? makeMatcap(THREE, true) : null;
  const matcapMatte = software ? makeMatcap(THREE, false) : null;

  const hemisphere = new THREE.HemisphereLight(0xeee6da, 0x2a2622, 0.5);
  scene.add(hemisphere);
  const key = new THREE.DirectionalLight(0xfff4e6, 2.2);
  key.position.set(0.5, 1, 0.65);
  key.castShadow = shadows;
  scene.add(key);
  scene.add(key.target);
  const fill = new THREE.DirectionalLight(0xbfd2ff, 0.6);
  fill.position.set(-0.8, 0.2, 0.6);
  scene.add(fill);

  const camera = new THREE.PerspectiveCamera(30, 1, 1, 10000);
  const controls = new Controls(camera, canvas);
  controls.enablePan = false;
  // Pas de zoom à la molette : elle doit continuer à faire défiler la page.
  controls.enableZoom = false;
  controls.enableDamping = false;
  controls.minPolarAngle = 0.05;
  controls.maxPolarAngle = Math.PI - 0.05;
  // OrbitControls demande `touch-action: none`, qui bloquerait le défilement au doigt : la page garde la verticale.
  canvas.style.touchAction = 'pan-y pinch-zoom';

  const surface = (parameters: ThreeNamespace.MeshStandardMaterialParameters): ThreeNamespace.MeshStandardMaterial => {
    if (!software) return new THREE.MeshStandardMaterial(parameters);
    const { color = 0xffffff, transparent = false, opacity = 1, depthWrite = true, side = THREE.FrontSide } = parameters;
    return new THREE.MeshMatcapMaterial({ color, transparent, opacity, depthWrite, side, matcap: (parameters.metalness ?? 0) > 0.5 ? matcapShiny : matcapMatte }) as unknown as ThreeNamespace.MeshStandardMaterial;
  };
  const material = surface;
  const materials: SharedMaterials = {
    steel: material({ color: 0xc8c6c2, metalness: 1, roughness: 0.3 }),
    forged: material({ color: 0x85817b, metalness: 0.9, roughness: 0.48 }),
    casting: material({ color: 0x7c7870, metalness: 0.55, roughness: 0.78 }),
    alloy: material({ color: 0xcbc9c4, metalness: 0.75, roughness: 0.42 }),
    rubber: material({ color: 0x1d1b19, metalness: 0, roughness: 0.82 }),
    brass: material({ color: 0xc69a52, metalness: 1, roughness: 0.35 }),
    accent: material({ color: 0xd06a2c, metalness: 0.1, roughness: 0.5 }),
    // Sur un rendu logiciel, une seule épaisseur : le verre est ce qui coûte le plus de pixels.
    ghost: material({ color: 0x9c9388, metalness: 0.1, roughness: 0.35, transparent: true, opacity: software ? 0.1 : 0.13, depthWrite: false, side: software ? THREE.FrontSide : THREE.DoubleSide }),
    light: new THREE.MeshBasicMaterial({ color: 0xffffff, transparent: true, depthWrite: false, blending: THREE.AdditiveBlending, toneMapped: false }),
  };

  let lost = false;
  let touched = false;
  let disposed = false;
  let fence: WebGLSync | null = null;
  let lastTheme: Theme | null = null;
  let lastAccent = '';
  const home = { yaw: 0.6, pitch: 0.3, zoom: 1 };
  let radius = 1;
  let zoom = 1;
  let width = 0;
  let height = 0;
  const spherical = new THREE.Spherical();
  const offset = new THREE.Vector3();
  const projected = new THREE.Vector3();

  /** Recule juste assez pour que le rayon voulu tienne dans le cadre, quelle que soit sa forme. */
  const distance = () => {
    const vertical = (camera.fov * Math.PI) / 180;
    const horizontal = 2 * Math.atan(Math.tan(vertical / 2) * camera.aspect);
    return radius / Math.sin(Math.min(vertical, horizontal) / 2) / zoom;
  };

  const place = (yaw: number, pitch: number) => {
    const d = distance();
    spherical.set(d, Math.PI / 2 - pitch, yaw);
    offset.setFromSpherical(spherical);
    camera.position.copy(controls.target).add(offset);
    camera.near = Math.max(d - radius * 2.2, d * 0.05);
    camera.far = d + radius * 3;
    camera.updateProjectionMatrix();
    controls.update();
  };

  const currentAngles = () => {
    offset.copy(camera.position).sub(controls.target);
    spherical.setFromVector3(offset);
    return { yaw: spherical.theta, pitch: Math.PI / 2 - spherical.phi };
  };

  const fitCanvas = () => {
    const w = canvas.clientWidth;
    const h = canvas.clientHeight;
    if (w === width && h === height) return;
    width = w;
    height = h;
    if (w === 0 || h === 0) return;
    // Sur un rendu logiciel, une vue large ne garde pas tous ses pixels : ils coûtent plus cher que la netteté n'apporte.
    renderer.setPixelRatio(software && w > SOFTWARE_WIDTH ? (ratioCap * SOFTWARE_WIDTH) / w : Math.min(window.devicePixelRatio || 1, ratioCap));
    renderer.setSize(w, h, false);
    camera.aspect = w / h;
    camera.updateProjectionMatrix();
    const { yaw, pitch } = currentAngles();
    place(yaw, pitch);
  };

  const onStart = () => {
    touched = true;
  };
  controls.addEventListener('start', onStart);

  const orbit = (dYaw: number, dPitch: number) => {
    const { yaw, pitch } = currentAngles();
    touched = true;
    place(yaw + dYaw, Math.min(1.5, Math.max(-1.5, pitch + dPitch)));
  };
  const zoomBy = (factor: number) => {
    zoom = Math.min(6, Math.max(0.6, zoom * factor));
    touched = true;
    const { yaw, pitch } = currentAngles();
    place(yaw, pitch);
  };
  const resetView = () => {
    zoom = home.zoom;
    touched = false;
    place(home.yaw, home.pitch);
  };

  const onKey = (event: KeyboardEvent) => {
    const step = 0.14;
    switch (event.key) {
      case 'ArrowLeft': orbit(-step, 0); break;
      case 'ArrowRight': orbit(step, 0); break;
      case 'ArrowUp': orbit(0, step); break;
      case 'ArrowDown': orbit(0, -step); break;
      case '+': case '=': zoomBy(1.15); break;
      case '-': zoomBy(1 / 1.15); break;
      case '0': case 'Home': resetView(); break;
      default: return;
    }
    event.preventDefault();
  };
  canvas.addEventListener('keydown', onKey);

  const onLost = (event: Event) => {
    event.preventDefault();
    lost = true;
    fence = null;
    options.onLost?.();
  };
  const onRestored = () => {
    if (disposed) return;
    lost = false;
    // three.js renvoie seul géométries et programmes. L'environnement, calculé, est à refaire.
    if (environment) {
      environment.dispose();
      environment = makeEnvironment(THREE, renderer);
      scene.environment = environment;
    }
    options.onRestored?.();
  };
  canvas.addEventListener('webglcontextlost', onLost);
  canvas.addEventListener('webglcontextrestored', onRestored);

  return {
    THREE,
    surface,
    renderer,
    scene,
    camera,
    controls,
    materials,
    software,
    get lost() {
      return lost;
    },
    get touched() {
      return touched;
    },
    frameOn(x, y, z, r, view) {
      controls.target.set(x, y, z);
      radius = r;
      if (view) {
        home.yaw = view.yaw ?? home.yaw;
        home.pitch = view.pitch ?? home.pitch;
        home.zoom = view.zoom ?? home.zoom;
      }
      key.target.position.set(x, y, z);
      key.position.set(x + r * 1.5, y + r * 3, z + r * 2);
      if (shadows) {
        const box = key.shadow.camera;
        box.left = box.bottom = -r * 1.3;
        box.right = box.top = r * 1.3;
        box.near = r * 0.5;
        box.far = r * 8;
        box.updateProjectionMatrix();
        key.shadow.mapSize.set(1024, 1024);
        key.shadow.normalBias = r * 0.004;
      }
      if (touched) {
        const { yaw, pitch } = currentAngles();
        place(yaw, pitch);
      } else {
        zoom = home.zoom;
        place(home.yaw, home.pitch);
      }
    },
    resetView,
    zoomBy,
    orbit,
    setTheme(theme) {
      // La chaleur change dans le même objet : seule la peinture d'accent la suit.
      if (theme.accent !== lastAccent) {
        lastAccent = theme.accent;
        const [ar, ag, ab] = parseColor(theme.accent);
        materials.accent.color.setRGB(ar, ag, ab, THREE.SRGBColorSpace);
      }
      if (theme === lastTheme) return;
      lastTheme = theme;
      const [r, g, b] = parseColor(theme.surface);
      const luminance = 0.2126 * r + 0.7152 * g + 0.0722 * b;
      const lightTheme = luminance > LIGHT_THEME_BACKGROUND;
      materials.ghost.color.set(lightTheme ? 0x4d5864 : 0xb4aa9e);
      materials.ghost.opacity = lightTheme ? 0.16 : 0.12;
      materials.casting.color.set(lightTheme ? 0x8d9296 : 0x77736c);
      scene.environmentIntensity = lightTheme ? 0.8 : 0.55;
      hemisphere.intensity = lightTheme ? 0.85 : 0.5;
    },
    busy() {
      if (!fence || lost || disposed) return false;
      const gl = renderer.getContext() as WebGL2RenderingContext;
      if (gl.isContextLost()) return false;
      const status = gl.getSyncParameter(fence, gl.SYNC_STATUS);
      if (status !== gl.SIGNALED) return true;
      gl.deleteSync(fence);
      fence = null;
      return false;
    },
    render() {
      const gl = renderer.getContext() as WebGL2RenderingContext;
      if (lost || disposed || gl.isContextLost()) return false;
      fitCanvas();
      if (width === 0 || height === 0) return true;
      renderer.render(scene, camera);
      if (fence) gl.deleteSync(fence);
      fence = gl.fenceSync(gl.SYNC_GPU_COMMANDS_COMPLETE, 0);
      gl.flush();
      return true;
    },
    project(out, x, y, z) {
      projected.set(x, y, z).project(camera);
      out[0] = projected.x * 0.5 + 0.5;
      out[1] = 0.5 - projected.y * 0.5;
      out[2] = projected.z < 1 ? 1 : 0;
    },
    dispose() {
      if (disposed) return;
      disposed = true;
      const gl = renderer.getContext() as WebGL2RenderingContext;
      if (fence && !gl.isContextLost()) gl.deleteSync(fence);
      fence = null;
      controls.removeEventListener('start', onStart);
      controls.dispose();
      canvas.removeEventListener('keydown', onKey);
      canvas.removeEventListener('webglcontextlost', onLost);
      canvas.removeEventListener('webglcontextrestored', onRestored);
      canvas.style.touchAction = '';
      disposeObject(scene);
      environment?.dispose();
      matcapShiny?.dispose();
      matcapMatte?.dispose();
      for (const item of Object.values(materials)) item.dispose();
      renderer.dispose();
      renderer.forceContextLoss();
    },
  };
}
