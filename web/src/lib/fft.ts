/** Transformée de Fourier rapide, sur place, pour les graphiques : tables préparées une fois par taille. */
export class Fft {
  readonly size: number;
  private cos: Float32Array;
  private sin: Float32Array;
  private reverse: Uint32Array;

  constructor(size: number) {
    this.size = size;
    this.cos = new Float32Array(size / 2);
    this.sin = new Float32Array(size / 2);
    for (let i = 0; i < size / 2; i++) {
      this.cos[i] = Math.cos((2 * Math.PI * i) / size);
      this.sin[i] = Math.sin((2 * Math.PI * i) / size);
    }
    this.reverse = new Uint32Array(size);
    const bits = Math.log2(size);
    for (let i = 0; i < size; i++) {
      let x = i;
      let r = 0;
      for (let b = 0; b < bits; b++) {
        r = (r << 1) | (x & 1);
        x >>= 1;
      }
      this.reverse[i] = r;
    }
  }

  transform(re: Float32Array, im: Float32Array): void {
    const { size, cos, sin, reverse } = this;
    for (let i = 0; i < size; i++) {
      const j = reverse[i];
      if (j > i) {
        const a = re[i];
        re[i] = re[j];
        re[j] = a;
        const b = im[i];
        im[i] = im[j];
        im[j] = b;
      }
    }
    for (let span = 2; span <= size; span *= 2) {
      const half = span / 2;
      const step = size / span;
      for (let start = 0; start < size; start += span) {
        for (let k = 0, w = 0; k < half; k++, w += step) {
          const c = cos[w];
          const s = -sin[w];
          const a = start + k;
          const b = a + half;
          const tr = re[b] * c - im[b] * s;
          const ti = re[b] * s + im[b] * c;
          re[b] = re[a] - tr;
          im[b] = im[a] - ti;
          re[a] += tr;
          im[a] += ti;
        }
      }
    }
  }
}
