# Online and LAN Notes (future work)

2026-10-14, slice 151. Research only; nothing implemented.

- Retail GT4 shipped without an official online service. It supports LAN
  battle for up to 6 consoles through the PS2 Network Adaptor (High
  confidence, secondary sources).
- The retail executable still contains network/online classes (mNetwork,
  mHttp, mNetConf, mDnas, mComm, PDIRTIME) and a network C stack (gt4-decomp
  `knowledge/architecture.md` §11, `classes.md` §5). Whether these are reachable
  beyond LAN is Unknown.
- Online existed only in test builds: PAPX-90523 (Japan, Online Test Version)
  and SCUS-97436 (North American public beta), servers active roughly June to
  September 2006 (Hidden Palace, Nenkai `gt-modding-hub` `docs/builds/gt4.md`).
  No revival server was found by this research; the owner reports one exists
  for PCSX2 (to be identified).
- Community LAN-over-internet for retail uses XLink Kai (PCSX2 DEV9 or real
  hardware).
- The donor runtime models no DEV9/SMAP/netcnf/ps2ip network hardware or IOP
  modules (Confirmed by grep).

Options for this port, cheapest first: (a) high-level emulation of the game's
socket calls on Winsock for the LAN path, located through decomp names;
(b) model DEV9/SMAP plus an IP stack on the IOP side; (c) a custom server after
the online protocol is understood through decompilation. First prove the retail
LAN path works end to end (for example in PCSX2) before porting it.
