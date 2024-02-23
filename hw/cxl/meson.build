softmmu_ss.add(when: 'CONFIG_CXL',
               if_true: files(
                   'cxl-component-utils.c',
                   'cxl-device-utils.c',
                   'cxl-mailbox-utils.c',
                   'cxl-host.c',
                   'cxl-cdat.c',
                   'cxl_type1_hcoh.c',
                   'cxl_type2_hcoh.c',
                   'cxl_hcache.c',
               ),
               if_false: files(
                   'cxl-host-stubs.c',
               ))

softmmu_ss.add(when: 'CONFIG_ALL', if_true: files('cxl-host-stubs.c'))
