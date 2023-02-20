# -*- coding: utf-8 -*-
"""
    HDF5 C code lexer
    ~~~~~~~~~~~

    Pygments lexer for C + HDF5 types.

"""

from pygments.lexers.c_cpp import CLexer
from pygments.token import Name, Keyword

class HDF5CLexer(CLexer):
    name = 'HDF5C'
    aliases = ['hdf5c']
    filenames = ['*.c'] # just to have one if you want to use

    EXTRA_KEYWORDS = ['hid_t', 'herr_t', 'hbool_t', 'hsize_t', 'htri_t', 'H5I_type_t', 'haddr_t', 'uuid_t', 'H5VL_token_t', 'H5R_ref_t', 'H5R_type_t', 'H5O_type_t', 'H5G_obj_t', 'hobj_ref_t', 'hdset_reg_ref_t', 'MPI_Comm', 'MPI_Info']

    def get_tokens_unprocessed(self, text):
        for index, token, value in CLexer.get_tokens_unprocessed(self, text):
            if token is Name and value in self.EXTRA_KEYWORDS:
                yield index, Keyword, value
            else:
                yield index, token, value
