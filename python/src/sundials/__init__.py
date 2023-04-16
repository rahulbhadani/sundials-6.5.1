try:
    from sundialspy import *
except ImportError:  # pragma: no cover
    print('Error: Could not find sundials C++ module.')
