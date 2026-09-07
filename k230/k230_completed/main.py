import sys, os
os.chdir("/sdcard")
sys.path.insert(0, "/sdcard")

MODE = "detect"

if MODE == "detect":
    import main_lvgl
    main_lvgl.main()
elif MODE == "dual":
    import main_dual
    main_dual.main()