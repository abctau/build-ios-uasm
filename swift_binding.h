#ifndef UASM_SWIFT_BINDING_H_
#define UASM_SWIFT_BINDING_H_

#ifdef __cplusplus
extern "C" {
#endif

#define UASM_SWIFT_API __attribute__((visibility("default")))

UASM_SWIFT_API double uasm_testUasm_add(double a, double b);

#ifdef __cplusplus
}
#endif

#endif  // UASM_SWIFT_BINDING_H_
