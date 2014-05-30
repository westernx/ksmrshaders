import os
import sys


def get_path(version=2014):
    '''Get the path to these shaders for stuffing into Maya envvars.'''

    return os.path.abspath(os.path.join(
        __file__,
        '..',
        'build',
        '%s-%d' % (
            'macosx' if sys.platform == 'darwin' else 'linux',
            version
        )
    ))
