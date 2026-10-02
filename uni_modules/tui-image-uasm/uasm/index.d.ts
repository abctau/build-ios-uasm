export interface TuiImageInfo {
	width: number
	height: number
	channels: number
}

export interface TuiImagePalette {
	width: number
	height: number
	dominant: number[]
	palette: number[][]
	edges: number[][]
}

export class TuiImageUasm {
	getInfo(bytes: Uint8Array | ArrayBuffer): TuiImageInfo
	resize(bytes: Uint8Array | ArrayBuffer, width: number, height: number, format?: string, quality?: number): Uint8Array
	crop(bytes: Uint8Array | ArrayBuffer, x: number, y: number, width: number, height: number, format?: string, quality?: number): Uint8Array
	rotate(bytes: Uint8Array | ArrayBuffer, degrees: number, format?: string, quality?: number): Uint8Array
	convert(bytes: Uint8Array | ArrayBuffer, format: string, quality?: number): Uint8Array
	getPalette(bytes: Uint8Array | ArrayBuffer, colorCount?: number): TuiImagePalette
}
