export class TuiSvgUasm {
	getInfo(bytes: Uint8Array | ArrayBuffer): TuiSvgInfo
	render(bytes: Uint8Array | ArrayBuffer, scale?: number): Uint8Array
	renderSize(bytes: Uint8Array | ArrayBuffer, width: number, height?: number): Uint8Array
}

export interface TuiSvgInfo {
	width: number
	height: number
}
