export class TuiPdfUasm {
	imagesToPdf(bytes: Uint8Array | ArrayBuffer, offsets: number[], pageSize?: string, orientation?: string, margin?: number): Uint8Array
	getPdfInfo(bytes: Uint8Array | ArrayBuffer): TuiPdfInfo
}

export interface TuiPdfInfo {
	version: string
	pageCount: number
	encrypted: boolean
}
