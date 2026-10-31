# Builds the UFSD manuals.  Needs typst and the bookmaster submodule
# (git submodule update --init docs/books/bookmaster).

TYPST ?= typst
BM    := bookmaster
# The PDF carries the edition: ml05-0001-1.pdf is the first edition of ML05-0001.
BOOKS := ml05-0001-1.pdf ml05-0002-1.pdf

.PHONY: all clean site

all: $(BOOKS)

ml05-0001-%.pdf: ml05-0001.typ $(wildcard guide/*.typ) $(BM)/bookmaster.typ
	$(TYPST) compile --root . --font-path $(BM)/fonts --ignore-system-fonts $< $@

ml05-0002-%.pdf: ml05-0002.typ $(wildcard ref/*.typ) $(BM)/bookmaster.typ
	$(TYPST) compile --root . --font-path $(BM)/fonts --ignore-system-fonts $< $@

# The web form, as Read the Docs builds it, into site/.
site:
	sh web/build.sh site

clean:
	rm -rf *.pdf site
