/** Largeur d'une colonne de chiffre et d'un séparateur, en fractions de la taille du texte, pour Big Shoulders. */
const DIGIT_EM = 0.49;
const SEPARATOR_EM = 0.24;

/**
 * Big Shoulders n'a pas de chiffres de largeur fixe : son « 1 » fait moitié moins large que les autres, et un nombre centré
 * sauterait à chaque changement. SVG permet de donner à chaque caractère sa propre position : chaque chiffre garde sa colonne.
 * Avec `text-anchor: middle`, chaque valeur de cette liste est le centre d'un caractère.
 */
export function digitColumns(text: string, center: number, fontSize: number): string {
  const wide = (char: string) => (char >= '0' && char <= '9') || char === '−' || char === '+';
  let total = 0;
  for (const char of text) total += wide(char) ? DIGIT_EM : SEPARATOR_EM;
  let at = center - (total * fontSize) / 2;
  let list = '';
  for (const char of text) {
    const width = (wide(char) ? DIGIT_EM : SEPARATOR_EM) * fontSize;
    list += `${list ? ' ' : ''}${(at + width / 2).toFixed(1)}`;
    at += width;
  }
  return list;
}
