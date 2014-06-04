{
    'targets': [
        {
            'target_name': 'pcsclite',
            'sources': [ 'src/addon.cpp', 'src/pcsclite.cpp', 'src/cardreader.cpp' ],
            'include_dirs': [
                '/usr/include/PCSC',
                '<!(node -e "require(\'nan\')")'
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
