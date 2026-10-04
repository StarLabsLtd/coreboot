	/dxe_core[.]h/ {
		if (getline <= 0 || $0 !~ /dxe_image_transaction[.]h/)
			exit 1
		found++
	}
	END { if (found == 0) exit 1 }
