// Case descriptor includes the default edge. Retain selector evaluation.
Kernel EmptySwitch;
concerns HIR::ControlFlow::Region;
concerns HIR::ControlFlow::Operation;
requires Optimization::ControlFlow::{EmptyCases, EmptySwitchSafe};
SelectSwitchDefault :: Binary(Term!resolves, Term!resolves) -> Binary
where EmptyCases($2) && EmptySwitchSafe { $1 `switch` $2 ~> $1 `jump_default` $2; }
