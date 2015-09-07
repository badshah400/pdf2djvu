# encoding=UTF-8

# Copyright © 2015 Jakub Wilk <jwilk@jwilk.net>
#
# This file is part of pdfdjvu.
#
# pdf2djvu is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# pdf2djvu is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.

from tools import (
    case,
    re,
)

class test(case):

    def test_no_title(self):
        self.pdf2djvu().assert_()
        r = self.ls()
        r.assert_(stdout=re(
            '\n'
            '\s*1\s+P\s+\d+\s+[\w.]+\n'
            '\s*2\s+P\s+\d+\s+[\w.]+\n'
        ))

    def test_ascii(self):
        template = '#{page}'
        self.pdf2djvu('--page-title-template', template).assert_()
        r = self.ls()
        r.assert_(stdout=re(
            '\n'
            '\s*1\s+P\s+\d+\s+[\w.]+\s+T=#1\n'
            '\s*2\s+P\s+\d+\s+[\w.]+\s+T=#2\n'
        ))

    def test_utf8(self):
        template = '№{page}'
        self.pdf2djvu('--page-title-template', template, encoding='UTF-8').assert_()
        r = self.ls()
        r.assert_(stdout=re(
            '\n'
            '\s*1\s+P\s+\d+\s+[\w.]+\s+T=№1\n'
            '\s*2\s+P\s+\d+\s+[\w.]+\s+T=№2\n'
        ))

    def test_bad_encoding(self):
        template = '{page}\xBA'
        r = self.pdf2djvu('--page-title-template', template, encoding='UTF-8')
        r.assert_(stderr=re('^Unable to convert page title to UTF-8:'), rc=1)

    def test_iso8859_1(self):
        template = '{page}\xBA'
        self.pdf2djvu('--page-title-template', template, encoding='ISO8859-1').assert_()
        r = self.ls()
        r.assert_(stdout=re(
            '\n'
            '\s*1\s+P\s+\d+\s+[\w.]+\s+T=1º\n'
            '\s*2\s+P\s+\d+\s+[\w.]+\s+T=2º\n'
        ))

# vim:ts=4 sts=4 sw=4 et
