interface NativePlugin {
  add(a: number, b: number): number
}

declare const plugin: NativePlugin

export = plugin
