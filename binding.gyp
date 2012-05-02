{
    'targets': [
        {
            'target_name': 'pcsclite',
            'sources': [ 'src/addon.cpp', 'src/pcsclite.cpp', 'src/cardreader.cpp' ],
            'include_dirs': [
                '/usr/include/PCSC'
            ],
            'link_settings': {
                'libraries': [
                  '-lpcsclite'
                ],
                'library_dirs': [
                  '/usr/lib'
                ]
            }
        }
    ]
}
