from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
DIRECT_CALL = (
    ROOT
    / "Bootstrap"
    / "Ultraviolet"
    / "src"
    / "05_codegen"
    / "llvm"
    / "emit"
    / "ir"
    / "call"
    / "direct.cpp"
)
LLVM_ATTR_HEADER = (
    ROOT
    / "Bootstrap"
    / "Ultraviolet"
    / "include"
    / "05_codegen"
    / "llvm"
    / "llvm_attr.h"
)
LLVM_ATTR = (
    ROOT
    / "Bootstrap"
    / "Ultraviolet"
    / "src"
    / "05_codegen"
    / "llvm"
    / "llvm_attr.cpp"
)
LLVM_CALL = (
    ROOT
    / "Bootstrap"
    / "Ultraviolet"
    / "src"
    / "05_codegen"
    / "llvm"
    / "llvm_call.cpp"
)


class DirectCallABIParamsTests(unittest.TestCase):
    def test_direct_call_lowering_uses_definition_abi_param_augmentation(self) -> None:
        source = DIRECT_CALL.read_text(encoding="utf-8")

        self.assertIn(
            "BuildProcABIParams(emitter, callee_symbol, proc_sig.params)",
            source,
            "direct calls must append the same hidden ABI parameters as procedure definitions",
        )

        declarations = re.findall(
            r"ABICallResult\s+abi\s*=\s*ComputeCallABI\(\s*emitter,\s*([^,]+),",
            source,
            re.MULTILINE,
        )
        self.assertIn(
            "call_abi_params",
            {arg.strip() for arg in declarations},
            "direct-call declaration synthesis must use augmented ABI params",
        )

        self.assertIn(
            "call_abi_params",
            re.search(
                r"call_result\s*=\s*EmitABICall\((?P<body>.*?)\);",
                source,
                re.DOTALL,
            ).group("body"),
            "direct-call emission must use augmented ABI params",
        )


class ABIParamAttributeTests(unittest.TestCase):
    def test_abi_param_attrs_apply_to_declarations_and_call_sites(self) -> None:
        header = LLVM_ATTR_HEADER.read_text(encoding="utf-8")
        attr_source = LLVM_ATTR.read_text(encoding="utf-8")
        call_source = LLVM_CALL.read_text(encoding="utf-8")
        direct_source = DIRECT_CALL.read_text(encoding="utf-8")

        self.assertIn("class CallBase;", header)
        self.assertIn("llvm::Function* func", header)
        self.assertIn("llvm::CallBase* call", header)
        self.assertIn("func->addParamAttrs", attr_source)
        self.assertIn("call->addParamAttrs", attr_source)
        self.assertIn(
            "ApplyABIParamAttrs(emitter.GetContext(), declared, decl_param_attrs)",
            direct_source,
        )
        self.assertIn(
            "ApplyABIParamAttrs(emitter.GetContext(), invoke_inst, abi.llvm_param_attrs)",
            call_source,
        )
        self.assertIn(
            "ApplyABIParamAttrs(emitter.GetContext(), call_inst, abi.llvm_param_attrs)",
            call_source,
        )


class SRetStorageAliasTests(unittest.TestCase):
    def test_preferred_sret_storage_rejects_live_call_input_aliases(self) -> None:
        source = LLVM_CALL.read_text(encoding="utf-8")

        self.assertIn(
            "preferred_result_storage_aliases_call_input",
            source,
            "preferred sret storage must be checked against live call inputs",
        )
        self.assertIn(
            "emitter.GetAddressableStorage((*source_args)[index])",
            source,
            "the sret alias guard must inspect addressable source argument storage",
        )
        self.assertIn(
            "implicit_panic_out_arg()",
            source,
            "the sret alias guard must include the hidden panic-out slot",
        )
        self.assertRegex(
            source,
            re.compile(
                r"preferred_result_storage\s*&&\s*ret_ty\s*&&\s*"
                r"preferred_result_storage->getType\(\)->isPointerTy\(\)\s*&&\s*"
                r"!preferred_result_storage_aliases_call_input"
                r"\(preferred_result_storage\)",
                re.MULTILINE,
            ),
            "EmitABICall must reject preferred sret storage that aliases a live input",
        )


class AArch64AggregateReturnCarrierTests(unittest.TestCase):
    def test_foreign_aggregate_return_carrier_handles_aarch64(self) -> None:
        source = LLVM_CALL.read_text(encoding="utf-8")

        self.assertIn("AArch64CAbiDirectAggregateReturnCarrier", source)
        self.assertRegex(
            source,
            re.compile(
                r"case project::TargetProfile::AArch64AAPCS64:\s*"
                r"case project::TargetProfile::AArch64Darwin:\s*"
                r"return AArch64CAbiDirectAggregateReturnCarrier",
                re.MULTILINE,
            ),
        )
        self.assertIn("const auto ret_align", source)
        self.assertIn(
            "ForeignAbiDirectAggregateCarrier(\n"
            "                emitter.GetTargetProfile(),\n"
            "                emitter.GetContext(),\n"
            "                result.ret_type,\n"
            "                *ret_size,\n"
            "                ret_align)",
            source,
        )


if __name__ == "__main__":
    unittest.main()
