"""CIR textual parser (.cir -> Module).

Module M3c.  Owner: Member 3.
Status: PLANNED -- Week 6.
"""
def parse_cir(text):
    """Parse the textual CIR emitted by printer.py.

    Round-trip property to satisfy:  print_module(parse_cir(t)) == t
    This is what lets the back ends be developed against checked-in .cir files
    without depending on the front end being finished.
    """
    raise NotImplementedError(
        "CIR text parser is scheduled for Week 6 (M3c, Member 3)")
