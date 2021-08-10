{
    'targets': [
        {
            'target_name': 'pcsclite',
            'sources': [ 'src/addon.cpp', 'src/pcsclite.cpp', 'src/cardreader.cpp' ],
            'cflags': [
                '-Wall',
                '-Wextra',
                '-Wno-unused-parameter',
                '-fPIC',
                '-fno-strict-aliasing',
                '-fno-exceptions',
                '-pedantic'
            ],
            'include_dirs': [
                '<!(node -p "require(\'node-addon-api\').include_dir")'
            ],
            'defines': [ 'NAPI_DISABLE_CPP_EXCEPTIONS' ],
            'conditions': [
                ['OS=="linux"', {
                    'include_dirs': [
                        '/usr/include/PCSC'
                    ],
                    'link_settings': {
                        'libraries': [ '-lpcsclite' ],
                        'library_dirs': [ '/usr/lib' ]
                    }
                }],
                ['OS=="mac"', {
                    'cflags+': ['-fvisibility=hidden'],
                    'xcode_settings': {
                        'GCC_SYMBOLS_PRIVATE_EXTERN': 'YES', # -fvisibility=hidden
                    },
                    'libraries': ['-framework', 'PCSC']
                }],
                ['OS=="win"', {
                    'libraries': ['-lWinSCard']
                }]
            ]
        }
    ]
}
