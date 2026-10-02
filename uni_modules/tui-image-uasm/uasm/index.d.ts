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
	flip(bytes: Uint8Array | ArrayBuffer, horizontal: boolean, vertical: boolean, format?: string, quality?: number): Uint8Array
	blur(bytes: Uint8Array | ArrayBuffer, radius: number, format?: string, quality?: number): Uint8Array
	roundCorners(bytes: Uint8Array | ArrayBuffer, radius: number, format?: string, quality?: number): Uint8Array
	circleClip(bytes: Uint8Array | ArrayBuffer, format?: string, quality?: number): Uint8Array
	extendFill(bytes: Uint8Array | ArrayBuffer, targetWidth: number, targetHeight: number, direction?: string, centerRatio?: number, fill?: string, blurRadius?: number, format?: string, quality?: number): Uint8Array
	edgeBlur(bytes: Uint8Array | ArrayBuffer, radius: number, direction?: string, regionTop?: number, regionClear?: number, regionBottom?: number, transition?: number, overlay?: boolean, overlayOpacity?: number, format?: string, quality?: number): Uint8Array
	progressiveBlur(bytes: Uint8Array | ArrayBuffer, direction: string, radius: number, offset?: number, interpolation?: number, format?: string, quality?: number): Uint8Array
	composite(baseBytes: Uint8Array | ArrayBuffer, overlayBytes: Uint8Array | ArrayBuffer, x: number, y: number, alpha?: number, format?: string, quality?: number): Uint8Array
}
