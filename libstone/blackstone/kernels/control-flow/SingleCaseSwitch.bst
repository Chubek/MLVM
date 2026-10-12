// Host proves exactly one case, valid integer comparison semantics and preserved
// selector evaluation, default edge, exceptional state and successor phis.
Kernel SingleCaseSwitch;
concerns HIR::ControlFlow::Region;
concerns HIR::ControlFlow::Operation;
requires Optimization::ControlFlow::SingleCaseSwitchSafe;
LowerSingleSwitchCase :: Binary(Term!resolves, Term!resolves) -> Binary
where SingleCaseSwitchSafe { $1 `switch` $2 ~> $1 `branch_case` $2; }
