from app.vector_store.sqlite_store import SqliteVectorStore


def test_interrupted_documents_are_marked_failed_after_restart(tmp_path) -> None:
    database_path = tmp_path / "rag.db"
    store = SqliteVectorStore(database_path)
    knowledge_base = store.create_knowledge_base("恢复测试", "", "default")
    document = store.create_document(
        knowledge_base.id,
        "document.txt",
        str(tmp_path / "document.txt"),
    )
    store.update_document_status(document.id, "processing")
    store.close()

    reopened = SqliteVectorStore(database_path)
    recovered_count = reopened.fail_incomplete_documents()
    recovered = reopened.get_document(knowledge_base.id, document.id)
    reopened.close()

    assert recovered_count == 1
    assert recovered.status == "failed"
    assert "service restart" in recovered.error_message
