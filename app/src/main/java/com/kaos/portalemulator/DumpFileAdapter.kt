package com.kaos.portalemulator

import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.TextView
import androidx.recyclerview.widget.RecyclerView
import java.io.File

class DumpFileAdapter(
    private val onFileClick: (File) -> Unit
) : RecyclerView.Adapter<DumpFileAdapter.FileViewHolder>() {

    private var files: List<File> = emptyList()

    fun updateFiles(newFiles: List<File>) {
        files = newFiles.sortedBy { it.name }
        notifyDataSetChanged()
    }

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): FileViewHolder {
        val view = LayoutInflater.from(parent.context)
            .inflate(android.R.layout.simple_list_item_2, parent, false)
        return FileViewHolder(view)
    }

    override fun onBindViewHolder(holder: FileViewHolder, position: Int) {
        holder.bind(files[position])
    }

    override fun getItemCount(): Int = files.size

    inner class FileViewHolder(itemView: View) : RecyclerView.ViewHolder(itemView) {
        private val nameText: TextView = itemView.findViewById(android.R.id.text1)
        private val sizeText: TextView = itemView.findViewById(android.R.id.text2)

        fun bind(file: File) {
            nameText.text = file.name
            sizeText.text = "${file.length()} bytes"
            
            itemView.setOnClickListener {
                onFileClick(file)
            }
        }
    }
}