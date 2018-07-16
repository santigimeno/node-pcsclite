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
            'conditions': [
                ['OS=="linux"', {
                    'include_dirs': [
                        '/usr/include/PCSC',
                        '<!@(command -v pkg-config >/dev/null 2>&1 && pkg-config libpcsclite --cflags-only-I | sed s/-I//g)'
                        '<!(node -e "require(\'nan\')")'
                    ],
                    'link_settings': {
                        'libraries': [ '-lpcsclite' ],
                        'library_dirs': [ '/usr/lib' ]
                    }
                }],
                ['OS=="mac"', {
                  'libraries': ['-framework', 'PCSC'],
                  "include_dirs" : [ "<!(node -e \"require('nan')\")" ]
                }],
                ['OS=="win"', {
                  'libraries': ['-lWinSCard'],
                  "include_dirs" : [ "<!(node -e \"require('nan')\")" ]
                }]
            ]
        }
    ]
}
