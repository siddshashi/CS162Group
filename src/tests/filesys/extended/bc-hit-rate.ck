# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected (IGNORE_EXIT_CODES => 1, [<<'EOF']);
(bc-hit-rate) begin
(bc-hit-rate) Create file "test".
(bc-hit-rate) Open file "test".
(bc-hit-rate) Total bytes read 10240.
(bc-hit-rate) Get hit rate for cold cache.
(bc-hit-rate) Reopen file "test".
(bc-hit-rate) Total bytes read 10240.
(bc-hit-rate) Get hit rate for hot cache.
(bc-hit-rate) Improved hit rate for hot cache.
(bc-hit-rate) end
EOF
pass;
