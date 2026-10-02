export interface TuiZipExtractResult {
	names: string[]
	datas: Uint8Array[]
}

export class TuiZipUasm {
	zipCreate(names: string[], contents: Uint8Array[]): Uint8Array
	zipList(zipBytes: Uint8Array | ArrayBuffer): string[]
	zipReadEntry(zipBytes: Uint8Array | ArrayBuffer, name: string): Uint8Array
	zipExtractAll(zipBytes: Uint8Array | ArrayBuffer): TuiZipExtractResult
	gzipCompress(bytes: Uint8Array | ArrayBuffer, level?: number): Uint8Array
	gzipDecompress(bytes: Uint8Array | ArrayBuffer): Uint8Array
}
