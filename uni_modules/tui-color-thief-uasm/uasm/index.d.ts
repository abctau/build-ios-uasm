interface TuiColorThiefPalette {
	width: number
	height: number
	dominant: number[]
	palette: number[][]
	edges: number[][]
}

interface TuiColorThiefPlugin {
	getPalette(bytes: Uint8Array | ArrayBuffer, colorCount?: number): TuiColorThiefPalette
}

declare const plugin: TuiColorThiefPlugin

export = plugin
