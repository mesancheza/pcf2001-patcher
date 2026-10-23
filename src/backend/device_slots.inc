// Default implementations of the unnamed device slots. Included inside class Device; generated together with
// device_interface.h. Constants match what the original software backend returns; other slots log their first
// call (with their raw arguments) and return 0.
// clang-format off
    uint32_t Slot031(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5) override { (void)a5; return Unimplemented(31, 5, a1, a2, a3, a4); }
    uint32_t Slot032(uint32_t a1) override { return Unimplemented(32, 1, a1, 0, 0, 0); }
    uint32_t Slot033(uint32_t a1) override { return Unimplemented(33, 1, a1, 0, 0, 0); }
    uint32_t Slot034(uint32_t a1) override { return Unimplemented(34, 1, a1, 0, 0, 0); }
    uint32_t Slot039() override { return Unimplemented(39, 0, 0, 0, 0, 0); }
    uint32_t Slot040(uint32_t a1, uint32_t a2) override { return Unimplemented(40, 2, a1, a2, 0, 0); }
    uint32_t Slot041(uint32_t a1, uint32_t a2) override { return Unimplemented(41, 2, a1, a2, 0, 0); }
    uint32_t Slot043(uint32_t a1) override { return Unimplemented(43, 1, a1, 0, 0, 0); }
    uint32_t Slot044() override { return Unimplemented(44, 0, 0, 0, 0, 0); }
    uint32_t Slot045(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5) override { (void)a5; return Unimplemented(45, 5, a1, a2, a3, a4); }
    uint32_t Slot046(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5) override { (void)a5; return Unimplemented(46, 5, a1, a2, a3, a4); }
    uint32_t Slot047(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7) override { (void)a5; (void)a6; (void)a7; return Unimplemented(47, 7, a1, a2, a3, a4); }
    uint32_t Slot048(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7) override { (void)a5; (void)a6; (void)a7; return Unimplemented(48, 7, a1, a2, a3, a4); }
    uint32_t Slot049() override { return Unimplemented(49, 0, 0, 0, 0, 0); }
    uint32_t Slot050() override { return Unimplemented(50, 0, 0, 0, 0, 0); }
    uint32_t Slot051() override { return Unimplemented(51, 0, 0, 0, 0, 0); }
    uint32_t Slot052() override { return 1; }
    uint32_t Slot053(uint32_t a1) override { return Unimplemented(53, 1, a1, 0, 0, 0); }
    uint32_t Slot061(uint32_t a1, uint32_t a2, uint32_t a3) override { (void)a1; (void)a2; (void)a3; return 0; }
    uint32_t Slot062(uint32_t a1) override { (void)a1; return 0; }
    uint32_t Slot064(uint32_t a1) override { (void)a1; return 0; }
    uint32_t Slot067() override { return 0; }
    uint32_t Slot068(uint32_t a1) override { (void)a1; return 0; }
    uint32_t Slot069(uint32_t a1) override { return Unimplemented(69, 1, a1, 0, 0, 0); }
    uint32_t Slot070(uint32_t a1) override { (void)a1; return 0; }
    uint32_t Slot071() override { return 0; }
    uint32_t Slot072(uint32_t a1, uint32_t a2) override { return Unimplemented(72, 2, a1, a2, 0, 0); }
    uint32_t Slot075(uint32_t a1, uint32_t a2) override { return Unimplemented(75, 2, a1, a2, 0, 0); }
    uint32_t Slot077(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4) override { return Unimplemented(77, 4, a1, a2, a3, a4); }
    uint32_t Slot079(uint32_t a1) override { (void)a1; return 1; }
    uint32_t Slot080(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6) override { (void)a5; (void)a6; return Unimplemented(80, 6, a1, a2, a3, a4); }
    uint32_t Slot081(uint32_t a1) override { (void)a1; return 0; }
    uint32_t Slot082(uint32_t a1) override { (void)a1; return 1; }
    uint32_t Slot083(uint32_t a1, uint32_t a2) override { (void)a1; (void)a2; return 0; }
    uint32_t Slot084(uint32_t a1, uint32_t a2) override { (void)a1; (void)a2; return 0; }
    uint32_t Slot085(uint32_t a1) override { (void)a1; return 0; }
    uint32_t Slot086() override { return 0; }
    uint32_t Slot087() override { return 1; }
    uint32_t Slot090(uint32_t a1, uint32_t a2, uint32_t a3) override { return Unimplemented(90, 3, a1, a2, a3, 0); }
    uint32_t Slot091(uint32_t a1) override { return Unimplemented(91, 1, a1, 0, 0, 0); }
    uint32_t Slot092(uint32_t a1, uint32_t a2) override { (void)a1; (void)a2; return 0; }
    uint32_t Slot093(uint32_t a1) override { (void)a1; return 1; }
    uint32_t Slot094(uint32_t a1, uint32_t a2) override { (void)a1; (void)a2; return 1; }
    uint32_t Slot095(uint32_t a1, uint32_t a2) override { (void)a1; (void)a2; return 1; }
    uint32_t Slot096(uint32_t a1, uint32_t a2) override { (void)a1; (void)a2; return 1; }
    uint32_t Slot097(uint32_t a1) override { (void)a1; return 0; }
    uint32_t Slot098(uint32_t a1) override { (void)a1; return 0; }
    uint32_t Slot100(uint32_t a1) override { return Unimplemented(100, 1, a1, 0, 0, 0); }
    uint32_t Slot103() override { return 1; }
    uint32_t Slot106() override { return 1; }
    uint32_t Slot108(uint32_t a1) override { return Unimplemented(108, 1, a1, 0, 0, 0); }
    uint32_t Slot109() override { return 1; }
    uint32_t Slot112(uint32_t a1) override { return Unimplemented(112, 1, a1, 0, 0, 0); }
    uint32_t Slot113() override { return 0; }
    uint32_t Slot114(uint32_t a1) override { (void)a1; return 0; }
    uint32_t Slot115() override { return 0; }
    uint32_t Slot118(uint32_t a1) override { return Unimplemented(118, 1, a1, 0, 0, 0); }
    uint32_t Slot121(uint32_t a1) override { (void)a1; return 1; }
    uint32_t Slot122() override { return 0; }
    uint32_t Slot123(uint32_t a1) override { (void)a1; return 0; }
    uint32_t Slot126(uint32_t a1) override { return Unimplemented(126, 1, a1, 0, 0, 0); }
    uint32_t Slot127() override { return 1; }
    uint32_t Slot128(uint32_t a1) override { (void)a1; return 1; }
    uint32_t Slot129() override { return 0; }
    uint32_t Slot130(uint32_t a1) override { (void)a1; return 1; }
    uint32_t Slot131(uint32_t a1) override { return Unimplemented(131, 1, a1, 0, 0, 0); }
    uint32_t Slot132() override { return 0; }
    uint32_t Slot133(uint32_t a1) override { (void)a1; return 0; }
    uint32_t Slot134() override { return 0; }
    uint32_t Slot136(uint32_t a1) override { (void)a1; return 1; }
    uint32_t Slot137(uint32_t a1) override { return Unimplemented(137, 1, a1, 0, 0, 0); }
    uint32_t Slot141() override { return 0; }
    uint32_t Slot142(uint32_t a1) override { (void)a1; return 0; }
    uint32_t Slot143() override { return 0; }
    uint32_t Slot144() override { return 0; }
    uint32_t Slot145(uint32_t a1) override { (void)a1; return 0; }
    uint32_t Slot146(uint32_t a1) override { (void)a1; return 0; }
    uint32_t Slot147() override { return 0; }
    uint32_t Slot148(uint32_t a1) override { (void)a1; return 0; }
    uint32_t Slot149(uint32_t a1, uint32_t a2) override { (void)a1; (void)a2; return 0; }
    uint32_t Slot150(uint32_t a1, uint32_t a2) override { (void)a1; (void)a2; return 0; }
    uint32_t Slot151() override { return 0; }
    uint32_t Slot152() override { return 0; }
    uint32_t Slot153(uint32_t a1, uint32_t a2) override { (void)a1; (void)a2; return 0; }
    uint32_t Slot154(uint32_t a1, uint32_t a2) override { (void)a1; (void)a2; return 0; }
    uint32_t Slot155(uint32_t a1) override { (void)a1; return 0; }
    uint32_t Slot156() override { return 0; }
    uint32_t Slot157() override { return 0; }
    uint32_t Slot158() override { return 0; }
    uint32_t Slot159(uint32_t a1) override { (void)a1; return 0; }
    uint32_t Slot160(uint32_t a1) override { return Unimplemented(160, 1, a1, 0, 0, 0); }
    uint32_t Slot164(uint32_t a1) override { return Unimplemented(164, 1, a1, 0, 0, 0); }
    uint32_t Slot167() override { return Unimplemented(167, 0, 0, 0, 0, 0); }
    uint32_t Slot170() override { return Unimplemented(170, 0, 0, 0, 0, 0); }
    uint32_t Slot171(uint32_t a1) override { return Unimplemented(171, 1, a1, 0, 0, 0); }
    uint32_t Slot172(uint32_t a1) override { return Unimplemented(172, 1, a1, 0, 0, 0); }
// clang-format on
