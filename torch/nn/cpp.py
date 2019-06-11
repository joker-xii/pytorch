"""Functionality for Python <-> C++ frontend inter-op."""

from torch import nn
from torch.nn.modules.module import _compute_should_move_tensor


class OrderedDictWrapper(object):
    """
    A wrapper around a C++ OrderedDict that dynamically evaluates the
    OrderedDict getter on a bound C++ module, such that new changes on the C++
    side are picked up. Otherwise accessing e.g. ``cpp_module._parameters`` just
    once would get a frozen copy of the parameters at the time of access.
    ``torch.nn.Module`` accesses ``_parameters`` et al. via ``self.__dict__`` so
    using properties does not work.
    """

    def __init__(self, cpp_module, attr):
        self.cpp_module = cpp_module
        self.attr = attr

    @property
    def cpp_dict(self):
        return getattr(self.cpp_module, self.attr)

    # Magic methods cannot be assigned dynamically and bypass ``getattr``, so we
    # must manually override them.

    def items(self):
        return self.cpp_dict.items()

    def keys(self):
        return self.cpp_dict.keys()

    def values(self):
        return self.cpp_dict.values()

    def __iter__(self):
        return self.cpp_dict.__iter__()

    def __len__(self):
        return self.cpp_dict.__len__()

    def __contains__(self, key):
        return self.cpp_dict.__contains__(key)

    def __getitem__(self, key):
        return self.cpp_dict.__getitem__(key)


class ModuleWrapper(nn.Module):
    """
    A subclass of ``torch.nn.Module`` that wraps a C++ frontend module and
    delegates all access.
    """

    def __init__(self, cpp_module):
        # Assign before the super class constructor so ``self.training`` can be
        # assigned to in the super class constructor.
        self.cpp_module = cpp_module
        super(ModuleWrapper, self).__init__()
        self._parameters = OrderedDictWrapper(cpp_module, "_parameters")
        self._buffers = OrderedDictWrapper(cpp_module, "_buffers")
        self._modules = OrderedDictWrapper(cpp_module, "_modules")
        for attr in dir(cpp_module):
            # Skip magic methods and the three attributes above.
            if not attr.startswith("_"):
                setattr(self, attr, getattr(self.cpp_module, attr))

    def _apply(self, fn, force_move_params_cpu_cuda=False):
        self._update_parameters(fn, force_move_params_cpu_cuda)

        for buf in self.buffers():
            buf_applied = fn(buf.data)
            if _compute_should_move_tensor(buf, buf_applied, force_move_tensor_cpu_cuda=force_move_params_cpu_cuda):
                self._buffers[key] = buf_applied
            else:
                buf.data = buf_applied

        return self

    @property
    def training(self):
        return self.cpp_module.training

    @training.setter
    def training(self, mode):
        self.cpp_module.train(mode)

    def __repr__(self):
        return self.cpp_module.__repr__()
