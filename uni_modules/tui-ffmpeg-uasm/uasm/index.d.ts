export class TuiFfmpegUasm {
	getMediaInfo(bytes: Uint8Array | ArrayBuffer): string
	extractFrame(bytes: Uint8Array | ArrayBuffer, timeMs: number, maxWidth: number): Uint8Array
	/**
	 * path 模式异步转码（仅 App 端）：options JSON 内 inputs 为文件路径数组、
	 * output 为目标路径；在异步线程完成读取/转码/写文件。
	 * resolve: '{"ok":true}'；reject: '{"ok":false,"error":"..."}'
	 */
	runJob(optionsJson: string): Promise<string>
}
