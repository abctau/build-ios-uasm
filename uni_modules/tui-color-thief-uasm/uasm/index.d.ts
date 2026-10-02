export interface TuiColorThiefPalette {
	width: number
	height: number
	dominant: number[]
	palette: number[][]
	edges: number[][]
}

export class TuiColorThiefUasm {
	getPalette(bytes: Uint8Array | ArrayBuffer, colorCount?: number): TuiColorThiefPalette
}
