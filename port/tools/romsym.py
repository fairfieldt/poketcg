"""Helpers to read the built ROM (poketcg.gbc) and its symbol file."""
import os, re

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))

class Rom:
    def __init__(self):
        self.data = open(os.path.join(ROOT, 'poketcg.gbc'), 'rb').read()
        self.sym = {}           # name -> (bank, addr)
        self.by_addr = {}       # (bank, addr) -> [names]
        for line in open(os.path.join(ROOT, 'poketcg.sym')):
            m = re.match(r'^([0-9a-f]{2}):([0-9a-f]{4}) (\S+)$', line.strip())
            if m:
                b, a, n = int(m.group(1), 16), int(m.group(2), 16), m.group(3)
                self.sym[n] = (b, a)
                self.by_addr.setdefault((b, a), []).append(n)

    @staticmethod
    def offset(bank, addr):
        if addr < 0x4000:
            return addr
        return bank * 0x4000 + (addr - 0x4000)

    def read(self, bank, addr, n):
        o = self.offset(bank, addr)
        return self.data[o:o + n]

    def byte(self, bank, addr):
        return self.data[self.offset(bank, addr)]

    def word(self, bank, addr):
        o = self.offset(bank, addr)
        return self.data[o] | self.data[o + 1] << 8

    def name_at(self, bank, addr, prefer_global=True):
        names = self.by_addr.get((bank, addr), [])
        if prefer_global:
            g = [n for n in names if '.' not in n]
            if g:
                return g[0]
        return names[0] if names else None

    def addr(self, name):
        return self.sym[name]
