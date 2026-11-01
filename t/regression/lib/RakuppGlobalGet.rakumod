# Reads it back as a dynamic variable, as Red's get-RED-DB does.
unit module RakuppGlobalGet;
sub get-default is export {
    return %*RAKUPP-DEFAULTS<default> if %*RAKUPP-DEFAULTS.defined and %*RAKUPP-DEFAULTS<default>:exists;
    'none'
}
