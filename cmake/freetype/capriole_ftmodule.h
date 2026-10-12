/* The FreeType modules compiled into Capriole (ADR 0005): the drivers of the font formats a PDF can
 * embed (§9.9) — TrueType and OpenType (sfnt), Type 1, CFF, CID-keyed Type 1 — and their PostScript
 * helpers. No rasterizers or hinters: Blend2D draws the outlines. Selected with FT_CONFIG_MODULES_H. */
FT_USE_MODULE(FT_Driver_ClassRec, tt_driver_class)
FT_USE_MODULE(FT_Driver_ClassRec, t1_driver_class)
FT_USE_MODULE(FT_Driver_ClassRec, cff_driver_class)
FT_USE_MODULE(FT_Driver_ClassRec, t1cid_driver_class)
FT_USE_MODULE(FT_Module_Class, psaux_module_class)
FT_USE_MODULE(FT_Module_Class, psnames_module_class)
FT_USE_MODULE(FT_Module_Class, sfnt_module_class)
