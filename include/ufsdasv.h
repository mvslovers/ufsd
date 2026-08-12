/* UFSDASV.H - ASVT membership scan
**
** Deliberately free of MVS dependencies: this header and its
** implementation (src/ufsd#asv.c) see the ASVT as nothing but an array
** of 32-bit words, so the same source compiles for cc370 and for a host
** compiler.  That is what makes the scan -- the one part of the #53
** liveness guard that can silently break (off-by-one, high-bit skip) --
** testable without an MVS round-trip (test/mvs/tstufsav.c,
** `make test-host`).
**
** Everything that navigates real control blocks (CVT -> ASVT, anchor ->
** server_ascb) stays in src/ufsd#rcl.c and is target-only.
*/

#ifndef UFSDASV_H
#define UFSDASV_H

/* ASVTAVAI/ASVTAVAL (ihaasvt.h): set in an entry means the ASID is
** AVAILABLE, i.e. NOT assigned to an address space.  An available entry
** carries the address of the next available entry alongside the bit, so
** the bit -- not a zero test -- is what separates the two kinds. */
#define UFSD_ASVT_AVAIL   0x80000000U

/* ============================================================
** ufsd_ascb_in_asvt
**
** Is `ascb` the ASCB of a currently assigned address space?
**
**   entries   &asvt->asvtenty[0] -- one word per ASID, ASID n at
**             index n-1 (ihaasvt.h)
**   nentries  asvt->asvtmaxu
**   ascb      candidate ASCB address as a 32-bit value
**
** Returns 1 if an assigned entry holds exactly this address, else 0.
**
** Pointer identity only: nothing is read from *ascb, so a stale pointer
** into reused SQA cannot mislead the caller through a field that is no
** longer an ASCB's.  A `0` address, a null table or an empty table all
** answer 0 -- callers must treat "not found" as "not proven live",
** never as "proven dead", because address reuse can also produce a
** false match (see src/ufsd#rcl.c).
** ============================================================ */
int ufsd_ascb_in_asvt(const unsigned *entries, unsigned nentries,
                      unsigned ascb);

#endif /* UFSDASV_H */
