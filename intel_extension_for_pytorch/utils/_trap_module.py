import torch
import intel_extension_for_pytorch._C


# utils function to define trapper base object
def _trap_module(name: str) -> type:
    def init_err(self):
        class_name = self.__class__.__name__
        raise RuntimeError(
            "Tried to instantiate trap base class {}.".format(class_name)
            + "\nIntel_extension_for_pytorch not compiled with XPU enabled.")
    return type(name, (object,), {"__init__": init_err})


def _register_trap(module: str):
    if not hasattr(intel_extension_for_pytorch._C, module):
        intel_extension_for_pytorch._C.__dict__[module] = _trap_module(module)


def _register_trap_ops(module: str):
    if not hasattr(torch.ops.torch_ipex, module):
        torch.ops.torch_ipex.__dict__[module] = _trap_module(module)


class trap_math_mode(object):
    FP32 = -1
    TF32 = -2
    BF32 = -3


# --- [ CPU traps:
_register_trap_ops('interaction_forward')


if not hasattr(intel_extension_for_pytorch._C, 'get_process_available_cores'):
    def no_cores():
        pass
    intel_extension_for_pytorch._C.__dict__['get_process_available_cores'] = no_cores


if not hasattr(intel_extension_for_pytorch._C, 'CPUPool'):
    class empty_cpu_pool(object):
        def __init__(self, ids):
            pass
        def get_core_list(self):
            pass
    intel_extension_for_pytorch._C.__dict__['CPUPool'] = empty_cpu_pool


if not hasattr(intel_extension_for_pytorch._C, 'FP32MathMode'):
    intel_extension_for_pytorch._C.__dict__['FP32MathMode'] = trap_math_mode


# --- [ XPU traps:
_register_trap('ShortStorageBase')
_register_trap('CharStorageBase')
_register_trap('IntStorageBase')
_register_trap('LongStorageBase')
_register_trap('BoolStorageBase')
_register_trap('HalfStorageBase')
_register_trap('DoubleStorageBase')
_register_trap('FloatStorageBase')
_register_trap('BFloat16StorageBase')
_register_trap('QUInt8StorageBase')
_register_trap('QInt8StorageBase')
_register_trap('_XPUStreamBase')
_register_trap('_XPUEventBase')


_register_trap_ops('convert_linear_weight_layout')
_register_trap_ops('convert_conv_weight_layout')
_register_trap_ops('convert_convtranspose_weight_layout')


if not hasattr(intel_extension_for_pytorch._C, 'XPUFP32MathMode'):
    intel_extension_for_pytorch._C.__dict__['XPUFP32MathMode'] = trap_math_mode