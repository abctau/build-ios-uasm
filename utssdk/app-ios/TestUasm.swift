import Darwin
import Foundation

private typealias AddFunction = @convention(c) (Double, Double) -> Double

private final class TestUasmAPI {
    let add: AddFunction
    private let handle: UnsafeMutableRawPointer

    private init(handle: UnsafeMutableRawPointer) throws {
        self.handle = handle
        self.add = try Self.symbol("uasm_testUasm_add", from: handle, as: AddFunction.self)
    }

    static let shared: TestUasmAPI = {
        let path = Bundle.main.privateFrameworksURL?.appendingPathComponent("UasmTestUasm.framework/UasmTestUasm").path ?? ""
        guard let handle = dlopen(path, RTLD_LAZY | RTLD_LOCAL) else { fatalError("unable to load UasmTestUasm.framework") }
        do { return try TestUasmAPI(handle: handle) } catch { fatalError("unable to load UasmTestUasm symbols") }
    }()

    private static func symbol<T>(_ name: String, from handle: UnsafeMutableRawPointer, as type: T.Type) throws -> T {
        guard let value = dlsym(handle, name) else { throw NSError(domain: "TestUasm", code: 1) }
        return unsafeBitCast(value, to: type)
    }
}

public enum TestUasm {
    public static func add(_ a: Double, _ b: Double) -> Double {
        TestUasmAPI.shared.add(a, b)
    }
}
