---
name: 3jsskill
description: Use when building a Three.js scene or interactive 3D page as static HTML with no Node, npm, Python or bundler step, aiming at realistic materials, lighting and performance, or when a 3D web page must work from a plain file or static host.
---

# 3jsskill: cenas 3D em Three.js sem build

Guia para criar páginas e cenas 3D que rodam no navegador como **arquivos estáticos**:
um `index.html`, o Three.js carregado de um CDN com versão fixa, e nada para compilar.
Não gera vídeo. Para MP4 sem navegador, use a skill `deltamotion` (C → BMP → MP4).

## Escopo

- **Serve para:** produto em 3D, cena de apresentação, hero de site, tour, visualizador
  de modelo GLTF, experiência com câmera e interação.
- **Não serve para:** renderizar vídeo (use `deltamotion`); páginas que precisam de
  servidor, banco ou SSR; 2D puro (use CSS ou canvas 2D).

## Regras de entrega

1. **Sem build.** Um `index.html` pode conter o CSS e o JS inline. Módulos próprios
   (`.js`) são aceitos, mas o HTML precisa servi-los pelo mesmo caminho.
2. **Versão fixa.** Use `three@0.187.0` (versão atual na data da skill). Antes de
   entregar, confirme a versão mais recente e troque o número nos dois endereços do
   importmap. Não use `latest`: uma atualização pode quebrar o visual.
3. **Importmap** para `three` e `three/addons/`. Os exemplos abaixo usam esse formato.
4. **Abertura.** Uma página com `import` de CDN costuma abrir com duplo clique, mas isso
   depende do navegador e não foi testado nesta skill. Módulos locais com `import`
   relativo precisam de HTTP. Se a página não carregar, hospede em qualquer host
   estático HTTPS. Não instale Node ou Python só para isso sem pedir ao usuário.
5. **Favicon.** Todo site nasce com favicon (regra do `webdesign`). Use um SVG inline
   com a marca do projeto.
6. **Um arquivo, um propósito.** A página tem uma cena principal. Conteúdo e controles
   ficam em HTML semântico por cima do canvas, nunca dentro dele.

## Exemplo mínimo (base realista)

```html
<!doctype html>
<html lang="pt-BR">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Cena 3D</title>
<link rel="icon" href="data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 32 32'%3E%3Ccircle cx='16' cy='16' r='12' fill='%23d9a25f'/%3E%3C/svg%3E">
<style>
  html, body { margin: 0; height: 100%; background: #0b0d12; overflow: hidden; }
  canvas { display: block; width: 100%; height: 100%; }
</style>
<script type="importmap">
{
  "imports": {
    "three": "https://cdn.jsdelivr.net/npm/three@0.187.0/build/three.module.js",
    "three/addons/": "https://cdn.jsdelivr.net/npm/three@0.187.0/examples/jsm/"
  }
}
</script>
</head>
<body>
<script type="module">
import * as THREE from 'three';
import { RoomEnvironment } from 'three/addons/environments/RoomEnvironment.js';

const renderer = new THREE.WebGLRenderer({ antialias: true });
renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
renderer.setSize(innerWidth, innerHeight);
renderer.outputColorSpace = THREE.SRGBColorSpace;
renderer.toneMapping = THREE.ACESFilmicToneMapping;
renderer.toneMappingExposure = 1.0;
renderer.shadowMap.enabled = true;
renderer.shadowMap.type = THREE.PCFSoftShadowMap;
document.body.appendChild(renderer.domElement);

const scene = new THREE.Scene();
scene.background = new THREE.Color(0x0b0d12);

// Ambiente sem arquivo externo: reflexos e luz vindos de uma sala procedural.
const pmrem = new THREE.PMREMGenerator(renderer);
scene.environment = pmrem.fromScene(new RoomEnvironment(), 0.04).texture;

const camera = new THREE.PerspectiveCamera(35, innerWidth / innerHeight, 0.1, 100);
camera.position.set(0, 1.2, 5);

const sol = new THREE.DirectionalLight(0xfff1dd, 2);
sol.position.set(3, 5, 2);
sol.castShadow = true;
sol.shadow.mapSize.set(2048, 2048);
sol.shadow.bias = -0.0005;
scene.add(sol);

const chao = new THREE.Mesh(
  new THREE.PlaneGeometry(20, 20),
  new THREE.MeshStandardMaterial({ color: 0x1b1d24, roughness: 0.9 })
);
chao.rotation.x = -Math.PI / 2;
chao.receiveShadow = true;
scene.add(chao);

const objeto = new THREE.Mesh(
  new THREE.SphereGeometry(0.9, 128, 64),
  new THREE.MeshPhysicalMaterial({ color: 0xd9a25f, metalness: 1, roughness: 0.28, clearcoat: 0.3 })
);
objeto.position.y = 0.9;
objeto.castShadow = true;
scene.add(objeto);

addEventListener('resize', () => {
  camera.aspect = innerWidth / innerHeight;
  camera.updateProjectionMatrix();
  renderer.setSize(innerWidth, innerHeight);
});

const movimentoReduzido = matchMedia('(prefers-reduced-motion: reduce)').matches;
renderer.setAnimationLoop((t) => {
  if (!movimentoReduzido) objeto.rotation.y = t * 0.0003;
  renderer.render(scene, camera);
});
</script>
</body>
</html>
```

Este exemplo foi escrito seguindo a documentação da versão 0.187.0 (arquivo
`RoomEnvironment.js` confirmado no CDN). **Não foi aberto em navegador nesta sessão.**
Antes de entregar, abra no navegador e confira o console.

## Realismo: o que realmente muda o resultado

| Decisão | Por que importa |
|---|---|
| Ambiente (`scene.environment` via PMREM) | É o maior ganho em metal, vidro e verniz. Sem ele, metais parecem pretos ou planos. `RoomEnvironment` resolve sem arquivo; um HDR real (via `RGBELoader`) é melhor quando houver asset. |
| `outputColorSpace = SRGBColorSpace` e tone mapping (ACES ou AgX) | Sem isso as cores ficam lavadas ou escuras demais. Escolha um tone mapping e ajuste `toneMappingExposure` na cena inteira. |
| Texturas de cor em sRGB, dados em linear | Albedo e emissivo: `texture.colorSpace = THREE.SRGBColorSpace`. Normal, rugosidade, metalicidade e AO: deixe linear (padrão). Trocar isso escurece ou estoura o material. |
| Anisotropia e mipmaps | Chão e madeira em ângulo rasante ficam nítidos com `texture.anisotropy = renderer.capabilities.getMaxAnisotropy()`. |
| Poucas luzes com sombra | Cada luz com sombra custa uma passada extra. Uma luz principal com sombra e o ambiente costumam bastar. |
| Ajuste de sombra | `shadow.bias` pequeno evita sombra falsa (acne) e `shadow.camera` justo evita perda de resolução. |
| Geometria com bordas | Objetos fabricados têm chanfro. Sem chanfro, a luz não pega a borda e o objeto parece de papel. Use segmentos suficientes só na silhueta. |
| Pós-processamento com moderação | Bloom com limiar alto, só para emissivos. Vinheta leve. Excesso dá aspecto de jogo. |
| Iluminação pré-calculada | Para arquitetura e interiores, lightmaps assados numa ferramenta de render costumam dar mais qualidade e custo menor que luz dinâmica. |
| Câmera de lente | FOV entre 25 e 40 graus parece câmera real; abertura muito larga distorce. Composição importa mais que polígonos. |

## Performance

- **DPR até 2.** Acima disso o custo sobe sem ganho visível.
- **Medição.** `renderer.info.render.calls` e `renderer.info.render.triangles` mostram o custo real por quadro.
- **Instancing** (`THREE.InstancedMesh`) para objetos repetidos, como árvores ou peças.
- **LOD** para modelos distantes.
- **Liberação.** Ao trocar de cena ou página: `geometry.dispose()`, `material.dispose()`, `texture.dispose()` e `renderer.dispose()`.
- **Sem alocação por quadro.** Reutilize vetores e objetos dentro do loop.
- **Parar quando invisível.** Pare a animação com `document.hidden` ou com `IntersectionObserver`.
- **Contexto perdido.** Escute `webglcontextlost` e mostre um poster estático.

## Acessibilidade e fallback

- `prefers-reduced-motion: reduce`: pare a animação e mantenha um quadro final composto.
- Sem WebGL, mostre uma imagem estática ou o texto do conteúdo. Não deixe a tela preta.
- O canvas recebe `aria-hidden="true"` se for decorativo. Controles e textos ficam fora dele, em HTML.

## Direção de arte

- Defina **uma ideia visual** antes do código: o que a cena diz e qual sensação deve deixar.
- Paleta de 2 a 3 cores. Fundo escuro e um acento quente costumam funcionar bem em produtos.
- Não empilhe efeitos. Cada um precisa de motivo: luz para dar forma, bloom para emissivo, grão só se a cena pedir.

## Como pedir a uma IA

Os exemplos de sites feitos com Claude Code que pesquisamos seguem um padrão que vale copiar:

1. Declare **arquivo único, sem build, sem npm**, com a versão do Three.js fixa.
2. Liste **cada estado** (rota, câmera, cena, tamanho de tela) e o que muda nele.
3. Termine com **critérios verificáveis**: "sem erros no console", "redimensionar não quebra",
   "respeita reduced motion", "60 fps em notebook comum".

Relatos de terceiros sobre sites "feitos por IA" são úteis como referência de processo.
Não atribua um resultado à IA sem a fonte, nem garanta realismo só pelo código.

## Verificação antes de entregar

- [ ] Versão do Three.js fixada e confirmada no CDN.
- [ ] Abre sem erro no console (testar no navegador real).
- [ ] Redimensionar a janela ajusta câmera e canvas.
- [ ] `prefers-reduced-motion` para a animação.
- [ ] Sem `TODO`, dados de exemplo visíveis ou favicon ausente.
- [ ] Medido: draw calls, triângulos e FPS na máquina do usuário.

## Limites

- Realismo depende de assets de qualidade (texturas, modelos, HDR). Código sozinho não basta.
- Sem navegador nesta skill, o código não é validado automaticamente. Teste no navegador real.
- WebGPU existe e tem API própria (`three/webgpu`); esta skill cobre WebGL.
- Gravar vídeo de uma página 3D exige gravação de tela em tempo real, que pode perder
  quadros. Para vídeo determinístico, recrie a animação com `deltamotion`.
