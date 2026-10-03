export class TuiFfmpegUasm {
	getMediaInfo(bytes: Uint8Array | ArrayBuffer): string
	extractFrame(bytes: Uint8Array | ArrayBuffer, timeMs: number, maxWidth: number): Uint8Array
	/**
	 * 同步转码（path 模式，仅 App 端）：options JSON 的 inputs 为文件路径数组，
	 * output 为目标路径；阻塞当前线程完成读取/转码/写文件。
	 * 返回 '{"ok":true}'；失败抛异常（含错误信息）。
	 */
	runJob(optionsJson: string): string
}
