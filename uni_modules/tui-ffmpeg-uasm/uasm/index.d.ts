export class TuiFfmpegUasm {
	getMediaInfo(bytes: Uint8Array | ArrayBuffer): string
	extractFrame(bytes: Uint8Array | ArrayBuffer, timeMs: number, maxWidth: number): Uint8Array
}
