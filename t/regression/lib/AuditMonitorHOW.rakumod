# Companion of ../exporthow-declare-monitor-locks.raku (issue #78): a cut-down
# OO::Monitors -- a `monitor` declarator, supplied through EXPORTHOW::DECLARE,
# whose metaclass adds a Lock attribute in new_type and wraps every method it
# is handed in add_method so that the method runs under that lock.
class MetamodelX::AuditMonitorHOW is Metamodel::ClassHOW {
    has $!lock-attr;

    method new_type(|) {
        my \type = callsame;
        type.HOW.setup_monitor_type(type);
        type
    }

    method setup_monitor_type(Mu \type) {
        $!lock-attr = Attribute.new(
            name => '$!MONITOR-lock', type => Any, package => type);
        self.add_attribute(type, $!lock-attr);
    }

    method add_method(Mu \type, $name, $meth) {
        unless $name eq 'BUILDALL' | 'POPULATE' | 'clone' {
            my $lock-attr := $!lock-attr;
            $meth.wrap: -> \SELF, | {
                if SELF.DEFINITE {
                    my $lock = $lock-attr.get_value(SELF);
                    $lock.lock();
                    LEAVE $lock.unlock();
                    callsame
                }
                else { callsame }
            }
        }
        self.Metamodel::ClassHOW::add_method(type, $name, $meth);
    }

    method compose(Mu \type) {
        my $lock-attr := $!lock-attr;
        self.add_method(type, 'POPULATE', method (|) {
            $lock-attr.set_value(self, Lock.new) unless $lock-attr.get_value(self);
            self
        }) unless self.declares_method(type, 'POPULATE');
        callsame
    }
}

my package EXPORTHOW {
    package DECLARE {
        constant monitor = MetamodelX::AuditMonitorHOW;
    }
}
