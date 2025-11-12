"""
Nova Python Bindings

Reusable Python bindings for Nova's Vulkan compute infrastructure.

Usage:
    from Nova.bindings.python import NovaPatternCatalog

    catalog = NovaPatternCatalog(embedding_dim=512, max_patterns=10000)
    catalog.add_pattern("pattern_001", embedding)
    results = catalog.find_similar(query, top_k=10)
"""

from .nova_bindings import NovaLibrary, NovaPatternCatalog

__version__ = "0.1.0"
__all__ = ["NovaLibrary", "NovaPatternCatalog"]
