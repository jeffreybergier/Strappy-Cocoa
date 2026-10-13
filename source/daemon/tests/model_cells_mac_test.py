#!/usr/bin/env python3
"""Build the isolated AppKit model-cell callback regression."""
import pathlib, re, subprocess
root=pathlib.Path(__file__).resolve().parents[3]
makefile=(root/'source/macOS/Makefile').read_text().replace('\\\n',' ')
sources=re.search(r'^GROUP_ui_SOURCES = (.*)$',makefile,re.M).group(1).split()
sources.remove('main.m')
sources.append(str(root/'source/daemon/tests/model_cells_mac_test.m'))
subprocess.run(['make','-C',str(root/'source/macOS'),'release','PRODUCTS=Strappy','Strappy_EMBED=',
    'BUILD_ROOT='+str(root/'build/tests/model-cells'), 'GROUP_ui_SOURCES='+' '.join(sources)],check=True)
